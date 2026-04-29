/* Framebuffer → ANSI renderer.
 *
 * Internal cell record (per terminal cell):
 *   - top_rgb      foreground colour (halfblock: top half pixel; ascii: glyph tint)
 *   - bot_rgb      background colour (halfblock: bottom half pixel; ascii: unused)
 *   - glyph_index  ramp slot (ascii) or 0 (halfblock — always uses ▀)
 *
 * Per frame we walk row-major, compare with the previous frame's cells,
 * and only emit cells that changed. Within a run of changes we suppress
 * SGR codes whose components match the last emitted SGR — that drops a
 * lot of bytes on large flat surfaces. After a skipped cell we mark the
 * cursor stale so the next emit will jump with a CUP escape. */

#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HALFBLOCK_UTF8 "\xe2\x96\x80"           /* U+2580 UPPER HALF BLOCK */
#define HALFBLOCK_UTF8_LEN 3

/* Mono halfblock can't use SGR to paint each half, so we pick from four
 * glyphs based on which of the two stacked pixels is "lit" (luminance
 * over a threshold). Index bits: bit0 = top lit, bit1 = bottom lit. */
static const char * const HALFBLOCK_MONO_GLYPHS[4] = {
    " ",                /* 0b00 — both dark              */
    "\xe2\x96\x80",     /* 0b01 — ▀ top lit              */
    "\xe2\x96\x84",     /* 0b10 — ▄ bottom lit           */
    "\xe2\x96\x88",     /* 0b11 — █ both lit             */
};
static const int HALFBLOCK_MONO_LENS[4] = { 1, 3, 3, 3 };
#define MONO_LIT_THRESHOLD 96

/* 10-step luminance ramp, dark → bright. Single byte per cell so the
 * ASCII modes work on any terminal that displays 7-bit text. */
static const char ASCII_RAMP[] = " .:-=+*#%@";
#define ASCII_RAMP_LEN ((int)(sizeof(ASCII_RAMP) - 1))

typedef struct {
    uint8_t top_r, top_g, top_b;
    uint8_t bot_r, bot_g, bot_b;
    uint8_t glyph_index;
    uint8_t valid;       /* 0 in the initial snapshot; forces a first-frame emit */
} term_cell;

struct term_render_ctx {
    /* Cell snapshots: prev = last frame written to the terminal,
     * curr  = freshly computed this frame. */
    term_cell *prev;
    term_cell *curr;
    int        cells_w;
    int        cells_h;

    /* Output byte buffer. Grows on demand; reused frame to frame. */
    char  *out;
    size_t out_cap;
    size_t out_len;

    int force_full;
};

/* ---- buffer helpers --------------------------------------------------- */

static void out_reserve(term_render_ctx *ctx, size_t extra) {
    if (ctx->out_len + extra <= ctx->out_cap) return;
    size_t cap = ctx->out_cap ? ctx->out_cap : 4096;
    while (cap < ctx->out_len + extra) cap *= 2;
    ctx->out = realloc(ctx->out, cap);
    ctx->out_cap = cap;
}

static void out_append(term_render_ctx *ctx, const char *s, size_t n) {
    out_reserve(ctx, n);
    memcpy(ctx->out + ctx->out_len, s, n);
    ctx->out_len += n;
}

static void out_append_str(term_render_ctx *ctx, const char *s) {
    out_append(ctx, s, strlen(s));
}

/* Tiny unsigned-int formatter; faster (and bounds-safe) than snprintf
 * inside the inner loop. n fits in uint16 for everything we emit. */
static void out_append_u(term_render_ctx *ctx, unsigned n) {
    char buf[6];
    int i = 0;
    if (n == 0) { buf[i++] = '0'; }
    else {
        char tmp[6]; int t = 0;
        while (n) { tmp[t++] = (char)('0' + (n % 10)); n /= 10; }
        while (t) buf[i++] = tmp[--t];
    }
    out_append(ctx, buf, (size_t)i);
}

/* ---- 256-colour quantization ----------------------------------------- */

/* xterm 256-colour palette has the structure:
 *   0..15    legacy ANSI colours
 *   16..231  6×6×6 RGB cube — index = 16 + 36*r + 6*g + b (each 0..5)
 *   232..255 24-step greyscale ramp from #080808 to #eeeeee
 * For images, the cube + greyscale ramp give clean results; we ignore
 * 0..15 because their colours vary by terminal theme. */
static int rgb_to_xterm256(uint8_t r, uint8_t g, uint8_t b) {
    /* Greyscale shortcut: if r≈g≈b, the 24-step ramp tracks luminance
     * better than the 6-cube. Threshold of 8 keeps "almost grey"
     * pixels out of the cube where they pick up tints. */
    int max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    if (max - min < 8) {
        int v = (r + g + b) / 3;
        if (v < 8)  return 16;       /* near-black → cube origin */
        if (v > 248) return 231;     /* near-white → cube top corner */
        return 232 + (v - 8) / 10;   /* linear scan of the 24-grey ramp */
    }
    int ri = (r * 5 + 127) / 255;
    int gi = (g * 5 + 127) / 255;
    int bi = (b * 5 + 127) / 255;
    return 16 + 36 * ri + 6 * gi + bi;
}

/* ---- cell construction ----------------------------------------------- */

static inline void unpack_argb(uint32_t p, uint8_t *r, uint8_t *g, uint8_t *b) {
    /* Renderers store ARGB8888 in native uint32_t order; raytracer fills
     * r=(p>>16)&0xff, g=(p>>8)&0xff, b=p&0xff. Match that. */
    *r = (uint8_t)((p >> 16) & 0xff);
    *g = (uint8_t)((p >>  8) & 0xff);
    *b = (uint8_t)( p        & 0xff);
}

static inline uint8_t luminance(uint8_t r, uint8_t g, uint8_t b) {
    /* Rec. 601 weights, integer math. */
    return (uint8_t)((54u * r + 183u * g + 19u * b) >> 8);
}

static void build_cells_halfblock(term_render_ctx *ctx,
                                  const uint32_t *pixels, int fb_w, int fb_h,
                                  int cells_w, int cells_h,
                                  term_color_mode color) {
    (void)fb_h;
    for (int cy = 0; cy < cells_h; cy++) {
        const uint32_t *top_row = pixels + (size_t)(cy * 2)     * fb_w;
        const uint32_t *bot_row = pixels + (size_t)(cy * 2 + 1) * fb_w;
        term_cell *row = ctx->curr + (size_t)cy * cells_w;
        for (int cx = 0; cx < cells_w; cx++) {
            term_cell *c = row + cx;
            unpack_argb(top_row[cx], &c->top_r, &c->top_g, &c->top_b);
            unpack_argb(bot_row[cx], &c->bot_r, &c->bot_g, &c->bot_b);
            if (color == TERM_COLOR_MONO) {
                int top_lit = luminance(c->top_r, c->top_g, c->top_b) >= MONO_LIT_THRESHOLD;
                int bot_lit = luminance(c->bot_r, c->bot_g, c->bot_b) >= MONO_LIT_THRESHOLD;
                c->glyph_index = (uint8_t)((bot_lit << 1) | top_lit);
            } else {
                c->glyph_index = 0;
            }
            c->valid = 1;
        }
    }
}

static void build_cells_ascii(term_render_ctx *ctx,
                              const uint32_t *pixels, int fb_w, int fb_h,
                              int cells_w, int cells_h) {
    (void)fb_h;
    for (int cy = 0; cy < cells_h; cy++) {
        const uint32_t *src = pixels + (size_t)cy * fb_w;
        term_cell *row = ctx->curr + (size_t)cy * cells_w;
        for (int cx = 0; cx < cells_w; cx++) {
            term_cell *c = row + cx;
            unpack_argb(src[cx], &c->top_r, &c->top_g, &c->top_b);
            c->bot_r = c->bot_g = c->bot_b = 0;
            uint8_t l = luminance(c->top_r, c->top_g, c->top_b);
            c->glyph_index = (uint8_t)((l * (ASCII_RAMP_LEN - 1)) / 255);
            c->valid = 1;
        }
    }
}

/* ---- emit helpers ----------------------------------------------------- */

/* SGR encoder. The "last_*" values track the most recently emitted
 * colour so we can suppress redundant codes within a run of changed
 * cells. Pass last_valid=0 on the first emit of a frame. */
typedef struct {
    int      cursor_stale;      /* next emit must precede with CUP */
    int      sgr_valid;
    uint8_t  last_fg_r, last_fg_g, last_fg_b;
    uint8_t  last_bg_r, last_bg_g, last_bg_b;
    int      last_fg_idx;       /* PALETTE256 only */
    int      last_bg_idx;
} emit_state;

static void emit_cup(term_render_ctx *ctx, int row, int col) {
    /* CSI <row> ; <col> H — both 1-based. */
    out_append(ctx, "\x1b[", 2);
    out_append_u(ctx, (unsigned)(row + 1));
    out_append(ctx, ";", 1);
    out_append_u(ctx, (unsigned)(col + 1));
    out_append(ctx, "H", 1);
}

static void emit_sgr_truecolor(term_render_ctx *ctx, emit_state *st,
                               const term_cell *c, term_glyph_mode glyph) {
    int need_fg = !st->sgr_valid ||
                  st->last_fg_r != c->top_r ||
                  st->last_fg_g != c->top_g ||
                  st->last_fg_b != c->top_b;
    int need_bg = (glyph == TERM_GLYPH_HALFBLOCK) &&
                  (!st->sgr_valid ||
                   st->last_bg_r != c->bot_r ||
                   st->last_bg_g != c->bot_g ||
                   st->last_bg_b != c->bot_b);
    if (!need_fg && !need_bg) return;

    out_append(ctx, "\x1b[", 2);
    int wrote = 0;
    if (need_fg) {
        out_append(ctx, "38;2;", 5);
        out_append_u(ctx, c->top_r); out_append(ctx, ";", 1);
        out_append_u(ctx, c->top_g); out_append(ctx, ";", 1);
        out_append_u(ctx, c->top_b);
        wrote = 1;
        st->last_fg_r = c->top_r;
        st->last_fg_g = c->top_g;
        st->last_fg_b = c->top_b;
    }
    if (need_bg) {
        if (wrote) out_append(ctx, ";", 1);
        out_append(ctx, "48;2;", 5);
        out_append_u(ctx, c->bot_r); out_append(ctx, ";", 1);
        out_append_u(ctx, c->bot_g); out_append(ctx, ";", 1);
        out_append_u(ctx, c->bot_b);
        st->last_bg_r = c->bot_r;
        st->last_bg_g = c->bot_g;
        st->last_bg_b = c->bot_b;
    }
    out_append(ctx, "m", 1);
    st->sgr_valid = 1;
}

static void emit_sgr_palette256(term_render_ctx *ctx, emit_state *st,
                                const term_cell *c, term_glyph_mode glyph) {
    int fg_idx = rgb_to_xterm256(c->top_r, c->top_g, c->top_b);
    int bg_idx = (glyph == TERM_GLYPH_HALFBLOCK)
               ? rgb_to_xterm256(c->bot_r, c->bot_g, c->bot_b)
               : -1;
    int need_fg = !st->sgr_valid || st->last_fg_idx != fg_idx;
    int need_bg = (bg_idx >= 0) && (!st->sgr_valid || st->last_bg_idx != bg_idx);
    if (!need_fg && !need_bg) return;

    out_append(ctx, "\x1b[", 2);
    int wrote = 0;
    if (need_fg) {
        out_append(ctx, "38;5;", 5);
        out_append_u(ctx, (unsigned)fg_idx);
        wrote = 1;
        st->last_fg_idx = fg_idx;
    }
    if (need_bg) {
        if (wrote) out_append(ctx, ";", 1);
        out_append(ctx, "48;5;", 5);
        out_append_u(ctx, (unsigned)bg_idx);
        st->last_bg_idx = bg_idx;
    }
    out_append(ctx, "m", 1);
    st->sgr_valid = 1;
}

static void emit_glyph(term_render_ctx *ctx, const term_cell *c,
                       const term_caps *caps) {
    if (caps->glyph == TERM_GLYPH_HALFBLOCK) {
        if (caps->color == TERM_COLOR_MONO) {
            int idx = c->glyph_index & 0x3;
            out_append(ctx, HALFBLOCK_MONO_GLYPHS[idx], HALFBLOCK_MONO_LENS[idx]);
        } else {
            out_append(ctx, HALFBLOCK_UTF8, HALFBLOCK_UTF8_LEN);
        }
    } else {
        char ch = ASCII_RAMP[c->glyph_index < ASCII_RAMP_LEN
                             ? c->glyph_index : ASCII_RAMP_LEN - 1];
        out_append(ctx, &ch, 1);
    }
}

static int cell_equal(const term_cell *a, const term_cell *b) {
    return a->valid && b->valid &&
           a->top_r == b->top_r && a->top_g == b->top_g && a->top_b == b->top_b &&
           a->bot_r == b->bot_r && a->bot_g == b->bot_g && a->bot_b == b->bot_b &&
           a->glyph_index == b->glyph_index;
}

/* ---- public API ------------------------------------------------------- */

term_render_ctx *term_render_create(void) {
    term_render_ctx *ctx = calloc(1, sizeof(*ctx));
    return ctx;
}

void term_render_destroy(term_render_ctx *ctx) {
    if (!ctx) return;
    free(ctx->prev);
    free(ctx->curr);
    free(ctx->out);
    free(ctx);
}

void term_render_pixel_size(const term_caps *caps, int cols, int rows,
                            int *fb_w, int *fb_h) {
    *fb_w = cols;
    *fb_h = (caps->glyph == TERM_GLYPH_HALFBLOCK) ? rows * 2 : rows;
}

void term_render_force_full_redraw(term_render_ctx *ctx) {
    ctx->force_full = 1;
}

static void resize_cell_grid(term_render_ctx *ctx, int cells_w, int cells_h) {
    if (ctx->cells_w == cells_w && ctx->cells_h == cells_h) return;
    free(ctx->prev);
    free(ctx->curr);
    size_t n = (size_t)cells_w * (size_t)cells_h;
    ctx->prev = calloc(n, sizeof(term_cell));
    ctx->curr = calloc(n, sizeof(term_cell));
    ctx->cells_w = cells_w;
    ctx->cells_h = cells_h;
    ctx->force_full = 1;
}

size_t term_render_frame(term_render_ctx *ctx,
                         const term_caps *caps,
                         const uint32_t *pixels, int fb_w, int fb_h,
                         int cols, int rows) {
    if (cols <= 0 || rows <= 0) { ctx->out_len = 0; return 0; }

    resize_cell_grid(ctx, cols, rows);

    if (caps->glyph == TERM_GLYPH_HALFBLOCK) {
        build_cells_halfblock(ctx, pixels, fb_w, fb_h, cols, rows, caps->color);
    } else {
        build_cells_ascii(ctx, pixels, fb_w, fb_h, cols, rows);
    }

    ctx->out_len = 0;

    /* Reset SGR state at frame start so the first emit always carries
     * full colour. Cheaper than tracking it across frames. */
    out_append(ctx, "\x1b[0m", 4);

    emit_state st = {0};
    st.cursor_stale = 1;
    int force = ctx->force_full;

    for (int cy = 0; cy < rows; cy++) {
        term_cell *prev_row = ctx->prev + (size_t)cy * cols;
        term_cell *curr_row = ctx->curr + (size_t)cy * cols;
        for (int cx = 0; cx < cols; cx++) {
            term_cell *cur = curr_row + cx;
            term_cell *pre = prev_row + cx;
            int unchanged = !force && cell_equal(pre, cur);
            if (unchanged) {
                /* Skip emit; cursor will need a CUP next time we DO emit. */
                st.cursor_stale = 1;
                continue;
            }
            if (st.cursor_stale) {
                emit_cup(ctx, cy, cx);
                st.cursor_stale = 0;
            }
            switch (caps->color) {
            case TERM_COLOR_TRUECOLOR:
                emit_sgr_truecolor(ctx, &st, cur, caps->glyph);
                break;
            case TERM_COLOR_PALETTE256:
                emit_sgr_palette256(ctx, &st, cur, caps->glyph);
                break;
            case TERM_COLOR_MONO:
                /* No SGR; the glyph alone carries information. */
                break;
            }
            emit_glyph(ctx, cur, caps);
        }
        st.cursor_stale = 1;     /* cursor at end-of-row is implementation-defined; force CUP next row */
    }

    /* Reset attributes at end so a Ctrl+C dump doesn't leave the
     * terminal in an exotic colour. */
    out_append(ctx, "\x1b[0m", 4);

    /* Snapshot current → prev for next frame's diff. */
    memcpy(ctx->prev, ctx->curr,
           (size_t)cols * (size_t)rows * sizeof(term_cell));
    ctx->force_full = 0;

    return ctx->out_len;
}

const char *term_render_buffer(const term_render_ctx *ctx) {
    return ctx->out;
}
