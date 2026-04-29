#ifndef TERM_H
#define TERM_H

#include <stddef.h>
#include <stdint.h>

/* =========================================================================
 * libs/term — render an ARGB framebuffer into an ANSI escape-sequence
 * stream for a terminal. Companion utilities for raw-mode termios setup,
 * alternate-screen lifecycle, capability detection, and a small input
 * parser so apps can run a 3D scene over ssh with no SDL/X involvement.
 *
 * Two glyph modes:
 *   HALFBLOCK — every cell is the upper-half-block char `▀` (U+2580).
 *               Foreground colour paints the top half, background paints
 *               the bottom half — two stacked RGB pixels per cell. The
 *               framebuffer the renderer expects is `cols × (rows*2)`.
 *   ASCII     — each cell is a glyph from a luminance ramp; the
 *               framebuffer is `cols × rows`. Foreground colour tints
 *               the glyph; background is left at the terminal default.
 *
 * Three colour modes:
 *   TRUECOLOR — 24-bit SGR (ESC[38;2;R;G;B m / ESC[48;2;R;G;B m).
 *   PALETTE256 — xterm 256-colour SGR (ESC[38;5;N m / ESC[48;5;N m),
 *                pixels quantized into the 6×6×6 cube + greyscale ramp.
 *   MONO      — no colour codes; glyphs only.
 *
 * Frame diffing: term_render_frame keeps a snapshot of the previous
 * frame's cells and skips re-emitting unchanged cells, which is what
 * makes the output viable over ssh.
 * ========================================================================= */

typedef enum {
    TERM_GLYPH_HALFBLOCK = 0,
    TERM_GLYPH_ASCII     = 1,
} term_glyph_mode;

typedef enum {
    TERM_COLOR_TRUECOLOR  = 0,
    TERM_COLOR_PALETTE256 = 1,
    TERM_COLOR_MONO       = 2,
} term_color_mode;

typedef struct {
    term_glyph_mode glyph;
    term_color_mode color;
} term_caps;

/* Inspect $LANG, $LC_ALL/$LC_CTYPE, $COLORTERM, $TERM and return the
 * best mode the terminal seems to support. Falls back gracefully:
 * no truecolor -> 256, no 256 -> mono; no UTF-8 -> ASCII ramp. Pure
 * read-only env probe. */
term_caps    term_caps_detect(void);
const char  *term_caps_glyph_name(term_glyph_mode m);
const char  *term_caps_color_name(term_color_mode m);

/* Override helpers for --mode=halfblock|ascii|auto and --color=...
 * Each takes a string and writes to *out; returns 1 on parse, 0 on
 * unknown. "auto" leaves *out untouched (caller pre-populates with
 * the detected default). */
int term_caps_parse_glyph(const char *s, term_glyph_mode *out);
int term_caps_parse_color(const char *s, term_color_mode *out);

/* ========================================================================
 *   Screen lifecycle (alt buffer + raw mode + cursor visibility + size)
 * ======================================================================== */

typedef struct term_screen term_screen;

/* Open the terminal for full-screen drawing on stdout.
 *   - Saves termios; switches stdin to raw, no-echo, non-blocking.
 *   - Switches the terminal to the alternate screen buffer.
 *   - Hides the cursor; clears the alt buffer.
 *   - Installs a SIGWINCH handler that just sets a flag.
 * On error returns NULL and leaves the terminal untouched.
 *
 * Symmetric: term_screen_close fully reverses every step (including
 * removing the SIGWINCH handler) and is safe to call from atexit /
 * signal handlers. */
term_screen *term_screen_open(void);
void         term_screen_close(term_screen *s);

/* Live terminal size in cells. Reads TIOCGWINSZ each call; cheap. */
void term_screen_size(int *cols, int *rows);

/* Returns 1 (and clears the flag) if SIGWINCH has fired since the last
 * call. The app should call term_render_force_full_redraw and re-poll
 * term_screen_size when this returns 1. */
int term_screen_consume_resize(term_screen *s);

/* Emit a CUP-home + the supplied byte buffer in one write(2). The
 * renderer's output is designed to be written verbatim. */
void term_screen_present(term_screen *s, const char *buf, size_t len);

/* ========================================================================
 *   Renderer
 * ======================================================================== */

typedef struct term_render_ctx term_render_ctx;

term_render_ctx *term_render_create(void);
void             term_render_destroy(term_render_ctx *ctx);

/* Given a capability set and a target cell grid, return the framebuffer
 * dimensions the caller should allocate and ask the raytracer to fill.
 *   HALFBLOCK: fb_w = cols, fb_h = rows * 2.
 *   ASCII:     fb_w = cols, fb_h = rows. */
void term_render_pixel_size(const term_caps *caps, int cols, int rows,
                            int *fb_w, int *fb_h);

/* Convert pixels into an ANSI byte stream stored inside ctx. Returns the
 * length; the buffer is owned by ctx and stable until the next call.
 * On the first call after creation (or after force_full_redraw) every
 * cell is emitted; on subsequent calls only changed cells are. */
size_t term_render_frame(term_render_ctx *ctx,
                         const term_caps *caps,
                         const uint32_t *pixels, int fb_w, int fb_h,
                         int cols, int rows);

/* Pointer to the byte stream produced by the most recent term_render_frame
 * call. NULL if none. Length matches the return value of that call. */
const char *term_render_buffer(const term_render_ctx *ctx);

/* Drop the previous-frame snapshot so the next term_render_frame emits
 * every cell. Call this after a resize or after the caller has cleared
 * the screen by external means. */
void term_render_force_full_redraw(term_render_ctx *ctx);

/* ========================================================================
 *   Input
 * ======================================================================== */

typedef enum {
    TERM_KEY_NONE = 0,
    TERM_KEY_CHAR,        /* .ch carries a printable byte (0x20..0x7e) */
    TERM_KEY_UP,
    TERM_KEY_DOWN,
    TERM_KEY_LEFT,
    TERM_KEY_RIGHT,
    TERM_KEY_ESC,
    TERM_KEY_ENTER,
    TERM_KEY_TAB,
    TERM_KEY_BACKSPACE,
} term_key_kind;

typedef struct {
    term_key_kind kind;
    int           ch;     /* TERM_KEY_CHAR: the byte. Otherwise unused. */
} term_key;

/* Non-blocking poll. Returns 1 and fills *out when a key event is
 * available, 0 otherwise. Bare ESC is reported as TERM_KEY_ESC; CSI
 * sequences (arrow keys, etc.) are decoded into their TERM_KEY_* values.
 * Stdin must be in raw, non-blocking mode (term_screen_open does this). */
int term_input_poll(term_key *out);

#endif /* TERM_H */
