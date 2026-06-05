/* Whisper overlay — see whisper.h. Self-contained: a tiny 5x7 font, a phrase
 * table, a fade state machine, and an alpha-composite onto an ARGB frame. */

#include "whisper.h"

#define FONT_W              5
#define FONT_H              7
#define FONT_GLYPH_COUNT    28
#define WHISPER_SCALE        2
#define WHISPER_FADE_IN_SEC  1.5f
#define WHISPER_HOLD_SEC     2.5f
#define WHISPER_FADE_OUT_SEC 2.0f
#define WHISPER_GAP_MIN_SEC 35.0f
#define WHISPER_GAP_MAX_SEC 55.0f
#define WHISPER_NOISE_PROB  20      /* percent: random-glyph instead of phrase */

/* Bit-packed 5×7 glyphs. Bit 4 (0x10) is the leftmost column. Index 0 is
 * space, index 1 is apostrophe, indices 2..27 are 'a'..'z'. */
static const uint8_t FONT_GLYPHS[FONT_GLYPH_COUNT][FONT_H] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space     */
    {0x04,0x04,0x04,0x00,0x00,0x00,0x00}, /* '         */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, /* a */
    {0x10,0x10,0x16,0x19,0x11,0x11,0x1E}, /* b */
    {0x00,0x00,0x0E,0x10,0x10,0x10,0x0E}, /* c */
    {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F}, /* d */
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, /* e */
    {0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, /* f */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, /* g */
    {0x10,0x10,0x16,0x19,0x11,0x11,0x11}, /* h */
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, /* i */
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, /* j */
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* k */
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, /* l */
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, /* m */
    {0x00,0x00,0x16,0x19,0x11,0x11,0x11}, /* n */
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, /* o */
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, /* p */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, /* q */
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, /* r */
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, /* s */
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, /* t */
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, /* u */
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, /* v */
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, /* w */
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, /* x */
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, /* y */
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, /* z */
};

static int font_index(char c) {
    if (c == ' ')  return 0;
    if (c == '\'') return 1;
    if (c >= 'a' && c <= 'z') return 2 + (c - 'a');
    return 0;
}

static const char *WHISPER_PHRASES[] = {
    "ph'nglui mglw'nafh",
    "they only sleep",
    "the angles wake",
    "the stars are wrong",
    "it remembers you",
    "yog sothoth",
    "we were here first",
    "the dream is real",
    "do not name it",
    "ia ia cthulhu",
};
#define WHISPER_PHRASE_COUNT \
    ((int)(sizeof(WHISPER_PHRASES) / sizeof(WHISPER_PHRASES[0])))

/* Self-contained 32-bit mixer; three orthogonal axes (seed, char index, row). */
static uint32_t whisper_hash(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 374761393u ^ b * 668265263u ^ c * 1274126177u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h;
}

static void whisper_pick_next(whisper_state *w, uint32_t seed) {
    uint32_t r = whisper_hash(seed, 0, 0xA5A5A5A5u);
    if ((r % 100u) < (uint32_t)(100 - WHISPER_NOISE_PROB)) {
        const char *src = WHISPER_PHRASES[(r >> 8) % WHISPER_PHRASE_COUNT];
        int len = 0;
        while (src[len] && len < WHISPER_MAX_LEN) {
            w->text[len] = src[len];
            len++;
        }
        w->text[len] = 0;
        w->text_len = len;
        w->is_noise = 0;
    } else {
        int len = 8 + (int)((r >> 16) % 7u);     /* 8..14 chars */
        for (int i = 0; i < len; i++) {
            w->text[i] = (char)('a' + (whisper_hash(seed, i, 1) % 26u));
        }
        w->text[len] = 0;
        w->text_len = len;
        w->is_noise = 1;
    }
    uint32_t rg = whisper_hash(seed, 1, 0xDEADBEEFu);
    w->next_gap = WHISPER_GAP_MIN_SEC
                + (WHISPER_GAP_MAX_SEC - WHISPER_GAP_MIN_SEC)
                  * ((rg & 0xFFFFu) / 65535.0f);
    w->noise_seed = seed;
}

void whisper_init(whisper_state *w, float first_delay_sec) {
    w->phase     = WHISPER_IDLE;
    w->phase_t   = 0.0f;
    w->text[0]   = 0;
    w->text_len  = 0;
    w->is_noise  = 0;
    w->noise_seed = 0;
    /* Hold off the first whisper until the caller's delay (e.g. the wake
     * fade) plus the normal minimum gap have passed. */
    w->next_gap  = first_delay_sec + WHISPER_GAP_MIN_SEC;
}

void whisper_update(whisper_state *w, float dt, uint32_t now_ms) {
    w->phase_t += dt;
    switch (w->phase) {
    case WHISPER_IDLE:
        if (w->phase_t >= w->next_gap) {
            whisper_pick_next(w, now_ms);
            w->phase = WHISPER_FADE_IN;
            w->phase_t = 0.0f;
        }
        break;
    case WHISPER_FADE_IN:
        if (w->phase_t >= WHISPER_FADE_IN_SEC) {
            w->phase = WHISPER_HOLD; w->phase_t = 0.0f;
        }
        break;
    case WHISPER_HOLD:
        if (w->phase_t >= WHISPER_HOLD_SEC) {
            w->phase = WHISPER_FADE_OUT; w->phase_t = 0.0f;
        }
        break;
    case WHISPER_FADE_OUT:
        if (w->phase_t >= WHISPER_FADE_OUT_SEC) {
            w->phase = WHISPER_IDLE; w->phase_t = 0.0f;
        }
        break;
    }
}

void whisper_render(const whisper_state *w, uint32_t *pixels, int W, int H) {
    if (w->phase == WHISPER_IDLE) return;
    float alpha;
    if (w->phase == WHISPER_FADE_IN) {
        float t = w->phase_t / WHISPER_FADE_IN_SEC;
        alpha = t * t * (3.0f - 2.0f * t);
    } else if (w->phase == WHISPER_HOLD) {
        alpha = 1.0f;
    } else {                                     /* FADE_OUT */
        float t = 1.0f - w->phase_t / WHISPER_FADE_OUT_SEC;
        if (t < 0.0f) t = 0.0f;
        alpha = t * t * (3.0f - 2.0f * t);
    }
    int alpha_i = (int)(alpha * 255.0f);
    if (alpha_i <= 0) return;
    if (alpha_i > 255) alpha_i = 255;

    int char_step = (FONT_W + 1) * WHISPER_SCALE;
    int text_w = w->text_len * char_step;
    int text_h = FONT_H * WHISPER_SCALE;
    int x0 = (W - text_w) / 2;
    int y0 = H - text_h - 16;

    const int fg_r = 200, fg_g = 220, fg_b = 230;   /* pale teal-white */

    for (int ci = 0; ci < w->text_len; ci++) {
        uint8_t glyph[FONT_H];
        if (w->is_noise) {
            for (int r = 0; r < FONT_H; r++) {
                glyph[r] = (uint8_t)(whisper_hash(w->noise_seed, ci, r) & 0x1F);
            }
        } else {
            int gi = font_index(w->text[ci]);
            for (int r = 0; r < FONT_H; r++) glyph[r] = FONT_GLYPHS[gi][r];
        }
        for (int gy = 0; gy < FONT_H; gy++) {
            uint8_t row = glyph[gy];
            if (!row) continue;
            for (int gx = 0; gx < FONT_W; gx++) {
                if (!(row & (1 << (FONT_W - 1 - gx)))) continue;
                int px0 = x0 + ci * char_step + gx * WHISPER_SCALE;
                int py0 = y0 + gy * WHISPER_SCALE;
                for (int sy = 0; sy < WHISPER_SCALE; sy++) {
                    int py = py0 + sy;
                    if (py < 0 || py >= H) continue;
                    for (int sx = 0; sx < WHISPER_SCALE; sx++) {
                        int px = px0 + sx;
                        if (px < 0 || px >= W) continue;
                        uint32_t p = pixels[py * W + px];
                        int b  = ( p        & 0xFF);
                        int g  = ((p >>  8) & 0xFF);
                        int r2 = ((p >> 16) & 0xFF);
                        b  = (b  * (255 - alpha_i) + fg_b * alpha_i) >> 8;
                        g  = (g  * (255 - alpha_i) + fg_g * alpha_i) >> 8;
                        r2 = (r2 * (255 - alpha_i) + fg_r * alpha_i) >> 8;
                        pixels[py * W + px] = (p & 0xFF000000u)
                                            | ((uint32_t)r2 << 16)
                                            | ((uint32_t)g  <<  8)
                                            |  (uint32_t)b;
                    }
                }
            }
        }
    }
}
