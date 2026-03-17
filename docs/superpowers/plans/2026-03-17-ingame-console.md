# In-Game Console Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Quake-style drop-down developer console to the battleforge engine with command input, game log viewing, and translucent overlay rendering.

**Architecture:** The engine (`battleforge.h/c`) gets a log ring buffer API (`bf_log`). The shell (`main.c`) gets a new `console.h/c` module that handles input capture, command parsing, bitmap font rendering, and overlay blitting. The renderer is untouched.

**Tech Stack:** C, SDL2, stb_image (for font PNG loading)

**Spec:** `docs/superpowers/specs/2026-03-17-ingame-console-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `libs/battleforge/battleforge.h` | Modify | Add log types and log API declarations |
| `libs/battleforge/battleforge.c` | Modify | Add log ring buffer and `bf_log`/`bf_log_count`/`bf_log_get` implementation |
| `apps/battleforge/console.h` | Create | Console state struct, init/destroy/input/render function declarations |
| `apps/battleforge/console.c` | Create | Console implementation: input handling, command parsing, bitmap font rendering, overlay blitting |
| `apps/battleforge/assets/font.png` | Create | 8x16 monospace bitmap font atlas (128 glyphs, 16 cols x 8 rows = 128x128 PNG) |
| `apps/battleforge/main.c` | Modify | Integrate console: init, input routing, render call |
| `apps/battleforge/Makefile.am` | Modify | Add `console.c` to sources |

---

### Task 1: Engine Log Ring Buffer

**Files:**
- Modify: `libs/battleforge/battleforge.h:1-97`
- Modify: `libs/battleforge/battleforge.c:1-76` (struct and includes area)

- [ ] **Step 1: Add log types to battleforge.h**

Add after the `bf_pick_result` struct (line 77), before the Engine section:

```c
/* --- Logging --- */

typedef enum {
    BF_LOG_INFO,
    BF_LOG_WARN,
    BF_LOG_ERROR
} bf_log_level;

#define BF_LOG_TEXT_SIZE 256
#define BF_LOG_BUFFER_SIZE 512

typedef struct {
    bf_log_level level;
    char text[BF_LOG_TEXT_SIZE];
} bf_log_entry;
```

Add after `bf_pick` declaration (line 95):

```c
/* --- Logging --- */
void            bf_log(bf_engine *e, bf_log_level level, const char *fmt, ...);
int             bf_log_count(const bf_engine *e);
const bf_log_entry *bf_log_get(const bf_engine *e, int index);
```

- [ ] **Step 2: Add log buffer fields to bf_engine struct**

In `battleforge.c`, add to `struct bf_engine` (after `int cmd_count;` around line 68):

```c
    /* Log ring buffer */
    bf_log_entry log_buffer[BF_LOG_BUFFER_SIZE];
    int log_write_pos;
    int log_count;
```

- [ ] **Step 3: Implement bf_log, bf_log_count, bf_log_get**

Add after `bf_command()` (after line 297) in `battleforge.c`:

```c
/* --- Logging --- */

void bf_log(bf_engine *e, bf_log_level level, const char *fmt, ...) {
    bf_log_entry *entry = &e->log_buffer[e->log_write_pos];
    entry->level = level;
    va_list args;
    va_start(args, fmt);
    vsnprintf(entry->text, BF_LOG_TEXT_SIZE, fmt, args);
    va_end(args);
    e->log_write_pos = (e->log_write_pos + 1) % BF_LOG_BUFFER_SIZE;
    if (e->log_count < BF_LOG_BUFFER_SIZE)
        e->log_count++;
}

int bf_log_count(const bf_engine *e) {
    return e->log_count;
}

const bf_log_entry *bf_log_get(const bf_engine *e, int index) {
    if (index < 0 || index >= e->log_count) return NULL;
    int pos = (e->log_write_pos - e->log_count + index + BF_LOG_BUFFER_SIZE)
              % BF_LOG_BUFFER_SIZE;
    return &e->log_buffer[pos];
}
```

Add `#include <stdarg.h>` to the includes at the top of `battleforge.c`.

- [ ] **Step 4: Add bf_log calls to existing command handlers**

Add logging to key handlers in `battleforge.c`:

```c
// In cmd_entity_create, after setting up the entity:
bf_log(e, BF_LOG_INFO, "entity %d created at (%.1f, %.1f, %.1f)",
       ent.id, ent.position.x, ent.position.y, ent.position.z);

// In cmd_entity_destroy, after deactivating:
bf_log(e, BF_LOG_INFO, "entity %d destroyed", cmd->entity_destroy.id);

// In cmd_entity_move, after setting target:
bf_log(e, BF_LOG_INFO, "entity %d moving to (%.1f, %.1f, %.1f)",
       cmd->entity_move.id, cmd->entity_move.position.x,
       cmd->entity_move.position.y, cmd->entity_move.position.z);

// In cmd_select:
if (cmd->select.id <= 0)
    bf_log(e, BF_LOG_INFO, "deselected");
else if (ent)
    bf_log(e, BF_LOG_INFO, "selected entity %d", cmd->select.id);

// In cmd_entity_create, when MAX_ENTITIES reached:
bf_log(e, BF_LOG_ERROR, "cannot create entity: max entities reached");
```

- [ ] **Step 5: Build and verify compilation**

Run: `cd /home/rafa/repos/c && make`
Expected: Clean build, no errors or warnings.

- [ ] **Step 6: Commit**

```bash
git add libs/battleforge/battleforge.h libs/battleforge/battleforge.c
git commit -m "feat(battleforge): add log ring buffer API (bf_log, bf_log_count, bf_log_get)"
```

---

### Task 2: Generate Bitmap Font Asset

**Files:**
- Create: `apps/battleforge/assets/font.png`

- [ ] **Step 1: Generate the font PNG programmatically**

Write a Python script (temporary, not committed) that generates a 128x128 PNG containing 128 ASCII glyphs in an 8x16 grid (16 columns x 8 rows). Use a basic monospace bitmap font. Each glyph is 8px wide x 16px tall. White glyphs on transparent background (so we can colorize at render time).

Run: `python3 generate_font.py`
Expected: `apps/battleforge/assets/font.png` created, 128x128 pixels.

- [ ] **Step 2: Verify the font PNG**

Visually inspect or check dimensions:
Run: `python3 -c "from PIL import Image; img = Image.open('apps/battleforge/assets/font.png'); print(img.size, img.mode)"`
Expected: `(128, 128) RGBA`

- [ ] **Step 3: Commit**

```bash
git add apps/battleforge/assets/font.png
git commit -m "feat(battleforge): add 8x16 bitmap font atlas PNG"
```

---

### Task 3: Console Module — State and Font Loading

**Files:**
- Create: `apps/battleforge/console.h`
- Create: `apps/battleforge/console.c`
- Modify: `apps/battleforge/Makefile.am`

- [ ] **Step 1: Create console.h with types and API**

```c
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include "battleforge.h"

#define CONSOLE_INPUT_SIZE 256
#define CONSOLE_MAX_SHELL_MSGS 128
#define CONSOLE_MSG_TEXT_SIZE 256
#define CONSOLE_HEIGHT 300       /* pixels from top of screen */
#define CONSOLE_FONT_W 8
#define CONSOLE_FONT_H 16

typedef struct {
    int open;
    char input[CONSOLE_INPUT_SIZE];
    int input_len;
    int cursor;
    int scroll_offset;

    /* Shell-generated messages (parse errors, help text) */
    char shell_messages[CONSOLE_MAX_SHELL_MSGS][CONSOLE_MSG_TEXT_SIZE];
    int shell_msg_log_cursor[CONSOLE_MAX_SHELL_MSGS]; /* engine log_count when msg was added */
    int shell_msg_count;      /* total messages stored (capped at max) */
    int shell_msg_write_pos;

    /* Bitmap font */
    uint32_t *font_pixels;    /* 128x128 RGBA pixel data */
    int font_loaded;

    /* Screen dimensions (set at init) */
    int screen_width;
    int screen_height;
} console_state;

int  console_init(console_state *cs, int screen_width, int screen_height,
                  const char *font_path);
void console_destroy(console_state *cs);
void console_toggle(console_state *cs);
int  console_is_open(const console_state *cs);
void console_handle_key(console_state *cs, int sdl_keycode, int sdl_scancode,
                        bf_engine *engine);
void console_handle_text(console_state *cs, const char *text);
void console_render(const console_state *cs, uint32_t *pixels,
                    int screen_width, int screen_height,
                    const bf_engine *engine);

#endif /* CONSOLE_H */
```

- [ ] **Step 2: Create console.c with init/destroy and font loading**

```c
#include "console.h"
#include "stb_image.h"    /* declarations only — implementation lives in slice.c */
#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

int console_init(console_state *cs, int screen_width, int screen_height,
                 const char *font_path) {
    memset(cs, 0, sizeof(*cs));
    cs->screen_width = screen_width;
    cs->screen_height = screen_height;

    /* Load bitmap font */
    int fw, fh, channels;
    unsigned char *data = stbi_load(font_path, &fw, &fh, &channels, 4);
    if (!data) {
        fprintf(stderr, "Console: failed to load font '%s'\n", font_path);
        return -1;
    }
    if (fw != 128 || fh != 128) {
        fprintf(stderr, "Console: unexpected font size %dx%d (expected 128x128)\n",
                fw, fh);
        stbi_image_free(data);
        return -1;
    }
    cs->font_pixels = (uint32_t *)data;
    cs->font_loaded = 1;
    return 0;
}

void console_destroy(console_state *cs) {
    if (cs->font_pixels) {
        stbi_image_free(cs->font_pixels);
        cs->font_pixels = NULL;
    }
    cs->font_loaded = 0;
}

void console_toggle(console_state *cs) {
    if (!cs->font_loaded) return;  /* disabled if font failed to load */
    cs->open = !cs->open;
}

int console_is_open(const console_state *cs) {
    return cs->open;
}
```

- [ ] **Step 3: Add console.c to Makefile.am**

In `apps/battleforge/Makefile.am`, change:
```
battleforge_SOURCES = main.c
```
to:
```
battleforge_SOURCES = main.c console.c
```

Also add `-I$(top_srcdir)/libs/slice` to `battleforge_CPPFLAGS` (needed for stb_image extern declarations, though slice is already linked).

- [ ] **Step 4: Build and verify**

Run: `cd /home/rafa/repos/c && make`
Expected: Clean build.

- [ ] **Step 5: Commit**

```bash
git add apps/battleforge/console.h apps/battleforge/console.c apps/battleforge/Makefile.am
git commit -m "feat(battleforge): add console module with font loading"
```

---

### Task 4: Console Rendering — Overlay and Font Blitting

**Files:**
- Modify: `apps/battleforge/console.c`

- [ ] **Step 1: Implement shell message helper**

Add to `console.c`. The shell message records the engine's current log count so we know where to interleave it in the display:

```c
static void console_shell_msg(console_state *cs, const bf_engine *engine,
                               const char *fmt, ...) {
    int idx = cs->shell_msg_write_pos;
    va_list args;
    va_start(args, fmt);
    vsnprintf(cs->shell_messages[idx], CONSOLE_MSG_TEXT_SIZE, fmt, args);
    va_end(args);
    cs->shell_msg_log_cursor[idx] = bf_log_count(engine);
    cs->shell_msg_write_pos = (cs->shell_msg_write_pos + 1) % CONSOLE_MAX_SHELL_MSGS;
    if (cs->shell_msg_count < CONSOLE_MAX_SHELL_MSGS)
        cs->shell_msg_count++;
}
```

- [ ] **Step 2: Implement alpha-blend overlay**

Add to `console.c`:

```c
static void console_draw_overlay(uint32_t *pixels, int screen_width,
                                  int console_height) {
    /* 30% scene + 70% dark tint (black) */
    for (int i = 0; i < screen_width * console_height; i++) {
        uint32_t p = pixels[i];
        uint8_t r = (((p >> 16) & 0xFF) * 77) >> 8;
        uint8_t g = (((p >> 8) & 0xFF) * 77) >> 8;
        uint8_t b = ((p & 0xFF) * 77) >> 8;
        pixels[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}
```

- [ ] **Step 3: Implement character blitting**

Add to `console.c`:

```c
static void console_draw_char(uint32_t *pixels, int screen_width,
                               int screen_height,
                               const uint32_t *font, int x, int y,
                               unsigned char c, uint32_t color) {
    if (c > 127) c = '?';
    int glyph_col = c % 16;
    int glyph_row = c / 16;
    int src_x = glyph_col * CONSOLE_FONT_W;
    int src_y = glyph_row * CONSOLE_FONT_H;

    for (int row = 0; row < CONSOLE_FONT_H; row++) {
        int dst_y = y + row;
        if (dst_y < 0 || dst_y >= screen_height) continue;
        for (int col = 0; col < CONSOLE_FONT_W; col++) {
            int dst_x = x + col;
            if (dst_x < 0 || dst_x >= screen_width) continue;
            uint32_t font_px = font[(src_y + row) * 128 + (src_x + col)];
            uint8_t alpha = (font_px >> 24) & 0xFF;
            if (alpha > 128) {
                pixels[dst_y * screen_width + dst_x] = color;
            }
        }
    }
}

static void console_draw_string(uint32_t *pixels, int screen_width,
                                 int screen_height,
                                 const uint32_t *font, int x, int y,
                                 const char *str, uint32_t color) {
    while (*str) {
        console_draw_char(pixels, screen_width, screen_height,
                          font, x, y, *str, color);
        x += CONSOLE_FONT_W;
        str++;
    }
}
```

- [ ] **Step 4: Implement console_render**

```c
void console_render(const console_state *cs, uint32_t *pixels,
                    int screen_width, int screen_height,
                    const bf_engine *engine) {
    if (!cs->open || !cs->font_loaded) return;

    int con_h = CONSOLE_HEIGHT;
    if (con_h > screen_height) con_h = screen_height;

    /* Dark overlay */
    console_draw_overlay(pixels, screen_width, con_h);

    uint32_t text_color = 0xFF00FF00u;   /* green */
    uint32_t error_color = 0xFFFF4444u;  /* red */
    uint32_t warn_color = 0xFFFFFF00u;   /* yellow */
    int line_h = CONSOLE_FONT_H;
    int pad_x = 4;

    /* Input line at bottom of console area */
    int input_y = con_h - line_h - 2;
    console_draw_string(pixels, screen_width, screen_height,
                        cs->font_pixels, pad_x, input_y,
                        "> ", text_color);
    console_draw_string(pixels, screen_width, screen_height,
                        cs->font_pixels, pad_x + 2 * CONSOLE_FONT_W, input_y,
                        cs->input, text_color);

    /* Blinking cursor */
    int cursor_x = pad_x + (2 + cs->cursor) * CONSOLE_FONT_W;
    /* Simple blink: use SDL_GetTicks modulo for timing.
       Since we don't have SDL here, just draw it solid.
       The shell can pass a blink flag if desired. */
    console_draw_char(pixels, screen_width, screen_height,
                      cs->font_pixels, cursor_x, input_y,
                      '_', text_color);

    /* Separator line above input */
    int sep_y = input_y - 2;
    for (int x = 0; x < screen_width; x++) {
        if (sep_y >= 0 && sep_y < screen_height)
            pixels[sep_y * screen_width + x] = 0xFF004400u;
    }

    /* Build display lines from engine logs + shell messages, bottom-to-top */
    int max_lines = (sep_y - 2) / line_h;
    int engine_total = bf_log_count(engine);
    int shell_total = cs->shell_msg_count;

    /* We need to merge engine logs and shell messages chronologically.
       Shell messages have shell_msg_log_cursor[i] indicating where in the
       engine log stream they were inserted.

       Simple approach: build a flat array of line pointers with colors,
       then render the last max_lines + scroll_offset lines. */
    int total_lines = engine_total + shell_total;
    if (total_lines == 0) return;

    /* Allocate temporary arrays for the merged display */
    typedef struct { const char *text; uint32_t color; } display_line;
    display_line *lines = malloc(total_lines * sizeof(display_line));
    if (!lines) return;

    int li = 0;  /* line index into merged output */
    int si = 0;  /* shell message index (oldest first) */

    /* Calculate oldest shell message index */
    int shell_oldest = 0;
    if (shell_total >= CONSOLE_MAX_SHELL_MSGS)
        shell_oldest = cs->shell_msg_write_pos;

    for (int ei = 0; ei < engine_total; ei++) {
        /* Insert any shell messages that belong before this engine log */
        while (si < shell_total) {
            int si_idx = (shell_oldest + si) % CONSOLE_MAX_SHELL_MSGS;
            if (cs->shell_msg_log_cursor[si_idx] <= ei) {
                lines[li++] = (display_line){
                    cs->shell_messages[si_idx], text_color
                };
                si++;
            } else {
                break;
            }
        }
        /* Add engine log */
        const bf_log_entry *entry = bf_log_get(engine, ei);
        if (entry) {
            uint32_t c = text_color;
            if (entry->level == BF_LOG_ERROR) c = error_color;
            else if (entry->level == BF_LOG_WARN) c = warn_color;
            lines[li++] = (display_line){ entry->text, c };
        }
    }
    /* Remaining shell messages */
    while (si < shell_total) {
        int si_idx = (shell_oldest + si) % CONSOLE_MAX_SHELL_MSGS;
        lines[li++] = (display_line){ cs->shell_messages[si_idx], text_color };
        si++;
    }

    /* Render visible lines, respecting scroll_offset */
    int visible_start = li - max_lines - cs->scroll_offset;
    if (visible_start < 0) visible_start = 0;
    int visible_end = visible_start + max_lines;
    if (visible_end > li) visible_end = li;

    int draw_y = sep_y - line_h;
    for (int i = visible_end - 1; i >= visible_start && draw_y >= 0; i--) {
        console_draw_string(pixels, screen_width, screen_height,
                            cs->font_pixels, pad_x, draw_y,
                            lines[i].text, lines[i].color);
        draw_y -= line_h;
    }

    free(lines);
}
```

- [ ] **Step 5: Build and verify**

Run: `cd /home/rafa/repos/c && make`
Expected: Clean build.

- [ ] **Step 6: Commit**

```bash
git add apps/battleforge/console.c
git commit -m "feat(battleforge): implement console overlay and bitmap font rendering"
```

---

### Task 5: Console Input Handling

**Files:**
- Modify: `apps/battleforge/console.c`

- [ ] **Step 1: Implement console_handle_text for printable characters**

```c
void console_handle_text(console_state *cs, const char *text) {
    while (*text && cs->input_len < CONSOLE_INPUT_SIZE - 1) {
        /* Insert at cursor */
        if (cs->cursor < cs->input_len) {
            memmove(&cs->input[cs->cursor + 1], &cs->input[cs->cursor],
                    cs->input_len - cs->cursor);
        }
        cs->input[cs->cursor] = *text;
        cs->cursor++;
        cs->input_len++;
        cs->input[cs->input_len] = '\0';
        text++;
    }
}
```

- [ ] **Step 2: Implement console_handle_key for special keys**

```c
void console_handle_key(console_state *cs, int sdl_keycode, int sdl_scancode,
                        bf_engine *engine) {
    (void)sdl_scancode;

    switch (sdl_keycode) {
    case SDLK_BACKSPACE:
        if (cs->cursor > 0) {
            memmove(&cs->input[cs->cursor - 1], &cs->input[cs->cursor],
                    cs->input_len - cs->cursor);
            cs->cursor--;
            cs->input_len--;
            cs->input[cs->input_len] = '\0';
        }
        break;

    case SDLK_DELETE:
        if (cs->cursor < cs->input_len) {
            memmove(&cs->input[cs->cursor], &cs->input[cs->cursor + 1],
                    cs->input_len - cs->cursor - 1);
            cs->input_len--;
            cs->input[cs->input_len] = '\0';
        }
        break;

    case SDLK_LEFT:
        if (cs->cursor > 0) cs->cursor--;
        break;

    case SDLK_RIGHT:
        if (cs->cursor < cs->input_len) cs->cursor++;
        break;

    case SDLK_HOME:
        cs->cursor = 0;
        break;

    case SDLK_END:
        cs->cursor = cs->input_len;
        break;

    case SDLK_RETURN:
        if (cs->input_len > 0) {
            console_execute(cs, engine);
            cs->input[0] = '\0';
            cs->input_len = 0;
            cs->cursor = 0;
            cs->scroll_offset = 0;  /* auto-scroll to bottom */
        }
        break;

    case SDLK_PAGEUP: {
        int max_lines = (CONSOLE_HEIGHT - CONSOLE_FONT_H - 4) / CONSOLE_FONT_H;
        int total = bf_log_count(engine) + cs->shell_msg_count;
        int max_scroll = total - max_lines;
        if (max_scroll < 0) max_scroll = 0;
        cs->scroll_offset += 5;
        if (cs->scroll_offset > max_scroll)
            cs->scroll_offset = max_scroll;
        break;
    }

    case SDLK_PAGEDOWN:
        cs->scroll_offset -= 5;
        if (cs->scroll_offset < 0) cs->scroll_offset = 0;
        break;
    }
}
```

Note: `console_execute` is fully implemented in the next task. For now, add a stub at the top of console.c so the file compiles:

```c
static void console_execute(console_state *cs, bf_engine *engine) {
    (void)cs;
    (void)engine;
    /* Stub — replaced in Task 6 */
}
```

- [ ] **Step 3: Build and verify**

Run: `cd /home/rafa/repos/c && make`
Expected: Clean build with the stub in place.

- [ ] **Step 4: Commit**

```bash
git add apps/battleforge/console.c
git commit -m "feat(battleforge): implement console input handling (keys and text)"
```

---

### Task 6: Command Parsing and Execution

**Files:**
- Modify: `apps/battleforge/console.c`

Replace the `console_execute` stub from Task 5 with the full implementation.

- [ ] **Step 1: Implement the command table and parser**

Add to `console.c`:

```c
#include <ctype.h>

/* --- Command parsing helpers --- */

/* Skip whitespace, return pointer to next token */
static const char *skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Parse a float from string, advance pointer. Returns 0 on failure. */
static int parse_float(const char **s, float *out) {
    const char *p = skip_ws(*s);
    char *end;
    *out = strtof(p, &end);
    if (end == p) return 0;
    *s = end;
    return 1;
}

static int parse_int(const char **s, int *out) {
    const char *p = skip_ws(*s);
    char *end;
    *out = (int)strtol(p, &end, 10);
    if (end == p) return 0;
    *s = end;
    return 1;
}

/* Check if input starts with prefix (case-insensitive), return rest */
static const char *match_prefix(const char *input, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*input) != tolower((unsigned char)*prefix))
            return NULL;
        input++;
        prefix++;
    }
    if (*input && !isspace((unsigned char)*input)) return NULL;
    return skip_ws(input);
}
```

- [ ] **Step 2: Implement individual command parsers**

```c
static void cmd_help(console_state *cs, bf_engine *engine, const char *args) {
    (void)args;
    console_shell_msg(cs, engine, "Available commands:");
    console_shell_msg(cs, engine, "  entity create <id> <sprite_id> <x> <y> <z> <dx> <dy> <dz> <speed>");
    console_shell_msg(cs, engine, "  entity destroy <id>");
    console_shell_msg(cs, engine, "  entity move <id> <x> <y> <z>");
    console_shell_msg(cs, engine, "  entity face <id> <dx> <dy> <dz>");
    console_shell_msg(cs, engine, "  entity speed <id> <speed>");
    console_shell_msg(cs, engine, "  entity animate <id> <anim_index>");
    console_shell_msg(cs, engine, "  camera set <x> <y> <z> <dx> <dy> <dz>");
    console_shell_msg(cs, engine, "  camera move <dx> <dy> <dz>");
    console_shell_msg(cs, engine, "  select <id>");
    console_shell_msg(cs, engine, "  help");
}

static void cmd_entity_create(console_state *cs, bf_engine *engine,
                               const char *args) {
    int id, sprite_id;
    float x, y, z, dx, dy, dz, speed;
    const char *p = args;
    if (!parse_int(&p, &id) || !parse_int(&p, &sprite_id) ||
        !parse_float(&p, &x) || !parse_float(&p, &y) || !parse_float(&p, &z) ||
        !parse_float(&p, &dx) || !parse_float(&p, &dy) || !parse_float(&p, &dz) ||
        !parse_float(&p, &speed)) {
        console_shell_msg(cs, engine,
            "Usage: entity create <id> <sprite_id> <x> <y> <z> <dx> <dy> <dz> <speed>");
        return;
    }
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_CREATE,
        .entity_create = { .id = id, .sprite_id = sprite_id,
                           .position = {x, y, z},
                           .direction = {dx, dy, dz},
                           .speed = speed }
    });
}

static void cmd_entity_destroy(console_state *cs, bf_engine *engine,
                                const char *args) {
    int id;
    const char *p = args;
    if (!parse_int(&p, &id)) {
        console_shell_msg(cs, engine, "Usage: entity destroy <id>");
        return;
    }
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_DESTROY,
        .entity_destroy = { .id = id }
    });
}

static void cmd_entity_move(console_state *cs, bf_engine *engine,
                             const char *args) {
    int id;
    float x, y, z;
    const char *p = args;
    if (!parse_int(&p, &id) || !parse_float(&p, &x) ||
        !parse_float(&p, &y) || !parse_float(&p, &z)) {
        console_shell_msg(cs, engine, "Usage: entity move <id> <x> <y> <z>");
        return;
    }
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_MOVE,
        .entity_move = { .id = id, .position = {x, y, z} }
    });
}

static void cmd_entity_face(console_state *cs, bf_engine *engine,
                             const char *args) {
    int id;
    float dx, dy, dz;
    const char *p = args;
    if (!parse_int(&p, &id) || !parse_float(&p, &dx) ||
        !parse_float(&p, &dy) || !parse_float(&p, &dz)) {
        console_shell_msg(cs, engine, "Usage: entity face <id> <dx> <dy> <dz>");
        return;
    }
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_FACE,
        .entity_face = { .id = id, .direction = {dx, dy, dz} }
    });
}

static void cmd_entity_speed(console_state *cs, bf_engine *engine,
                              const char *args) {
    int id;
    float speed;
    const char *p = args;
    if (!parse_int(&p, &id) || !parse_float(&p, &speed)) {
        console_shell_msg(cs, engine, "Usage: entity speed <id> <speed>");
        return;
    }
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_SET_SPEED,
        .entity_set_speed = { .id = id, .speed = speed }
    });
}

static void cmd_entity_animate(console_state *cs, bf_engine *engine,
                                const char *args) {
    int id, anim;
    const char *p = args;
    if (!parse_int(&p, &id) || !parse_int(&p, &anim)) {
        console_shell_msg(cs, engine, "Usage: entity animate <id> <anim_index>");
        return;
    }
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_ANIMATE,
        .entity_animate = { .id = id, .anim_index = anim }
    });
}

static void cmd_camera_set(console_state *cs, bf_engine *engine,
                            const char *args) {
    float x, y, z, dx, dy, dz;
    const char *p = args;
    if (!parse_float(&p, &x) || !parse_float(&p, &y) || !parse_float(&p, &z) ||
        !parse_float(&p, &dx) || !parse_float(&p, &dy) || !parse_float(&p, &dz)) {
        console_shell_msg(cs, engine, "Usage: camera set <x> <y> <z> <dx> <dy> <dz>");
        return;
    }
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_CAMERA_SET,
        .camera_set = { .position = {x, y, z}, .direction = {dx, dy, dz} }
    });
}

static void cmd_camera_move(console_state *cs, bf_engine *engine,
                             const char *args) {
    float dx, dy, dz;
    const char *p = args;
    if (!parse_float(&p, &dx) || !parse_float(&p, &dy) || !parse_float(&p, &dz)) {
        console_shell_msg(cs, engine, "Usage: camera move <dx> <dy> <dz>");
        return;
    }
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_CAMERA_MOVE,
        .camera_move = { .delta = {dx, dy, dz} }
    });
}

static void cmd_select(console_state *cs, bf_engine *engine,
                        const char *args) {
    int id;
    const char *p = args;
    if (!parse_int(&p, &id)) {
        console_shell_msg(cs, engine, "Usage: select <id>");
        return;
    }
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_SELECT,
        .select = { .id = id }
    });
}
```

- [ ] **Step 3: Implement console_execute dispatch**

```c
static void console_execute(console_state *cs, bf_engine *engine) {
    const char *input = skip_ws(cs->input);
    if (*input == '\0') return;

    const char *rest;

    /* Two-word commands first (entity X, camera X) */
    if ((rest = match_prefix(input, "entity"))) {
        const char *sub;
        if ((sub = match_prefix(rest, "create")))
            cmd_entity_create(cs, engine, sub);
        else if ((sub = match_prefix(rest, "destroy")))
            cmd_entity_destroy(cs, engine, sub);
        else if ((sub = match_prefix(rest, "move")))
            cmd_entity_move(cs, engine, sub);
        else if ((sub = match_prefix(rest, "face")))
            cmd_entity_face(cs, engine, sub);
        else if ((sub = match_prefix(rest, "speed")))
            cmd_entity_speed(cs, engine, sub);
        else if ((sub = match_prefix(rest, "animate")))
            cmd_entity_animate(cs, engine, sub);
        else
            console_shell_msg(cs, engine, "Unknown entity command. Type 'help'.");
    } else if ((rest = match_prefix(input, "camera"))) {
        const char *sub;
        if ((sub = match_prefix(rest, "set")))
            cmd_camera_set(cs, engine, sub);
        else if ((sub = match_prefix(rest, "move")))
            cmd_camera_move(cs, engine, sub);
        else
            console_shell_msg(cs, engine, "Unknown camera command. Type 'help'.");
    } else if ((rest = match_prefix(input, "select"))) {
        cmd_select(cs, engine, rest);
    } else if (match_prefix(input, "help")) {
        cmd_help(cs, engine, "");
    } else {
        console_shell_msg(cs, engine, "Unknown command: %s", cs->input);
    }
}
```

- [ ] **Step 4: Build and verify**

Run: `cd /home/rafa/repos/c && make`
Expected: Clean build.

- [ ] **Step 5: Commit**

```bash
git add apps/battleforge/console.c
git commit -m "feat(battleforge): implement console command parsing and execution"
```

---

### Task 7: Shell Integration

**Files:**
- Modify: `apps/battleforge/main.c`

- [ ] **Step 1: Add console include and state**

At top of `main.c`, add after other includes:
```c
#include "console.h"
```

After `int running = 1;` (line 367), add:
```c
    console_state console;
    if (console_init(&console, WINDOW_W, WINDOW_H,
                     "apps/battleforge/assets/font.png") < 0) {
        fprintf(stderr, "Warning: console disabled (font not found)\n");
    }
```

Before `SDL_Quit()` in cleanup (around line 463), add:
```c
    console_destroy(&console);
```

- [ ] **Step 2: Enable SDL text input**

After `console_init`, add:
```c
    SDL_StartTextInput();
```

- [ ] **Step 3: Route input through console in event loop**

Replace the existing event handling block (lines 376-411) with console-aware routing:

```c
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;

            /* Backtick toggles console */
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_BACKQUOTE) {
                console_toggle(&console);
                continue;
            }

            if (console_is_open(&console)) {
                /* Console captures all input */
                if (e.type == SDL_KEYDOWN) {
                    console_handle_key(&console, e.key.keysym.sym,
                                       e.key.keysym.scancode, engine);
                } else if (e.type == SDL_TEXTINPUT) {
                    /* Filter out backtick from text input */
                    if (e.text.text[0] != '`')
                        console_handle_text(&console, e.text.text);
                }
                continue;  /* Don't pass to game */
            }

            /* Normal game input when console is closed */
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                bf_pick_result pick = bf_pick(engine, e.button.x, e.button.y);
                if (e.button.button == SDL_BUTTON_LEFT) {
                    if (pick.type == BF_PICK_ENTITY) {
                        selected_id = pick.entity_id;
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_SELECT,
                            .select = { .id = pick.entity_id }
                        });
                        fprintf(stderr, "Selected entity %d\n", pick.entity_id);
                    } else {
                        selected_id = 0;
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_SELECT,
                            .select = { .id = 0 }
                        });
                        fprintf(stderr, "Deselected\n");
                    }
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    if (selected_id > 0 && pick.type == BF_PICK_GROUND) {
                        vector dest = pick.position;
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_ENTITY_MOVE,
                            .entity_move = { .id = selected_id,
                                             .position = dest }
                        });
                        fprintf(stderr, "Move entity %d to (%.1f, %.1f, %.1f)\n",
                                selected_id, dest.x, dest.y, dest.z);
                    }
                }
            }
        }
```

- [ ] **Step 4: Suppress polled camera input when console is open**

Wrap the camera input block (lines 413-427) with:

```c
        if (!console_is_open(&console)) {
            const Uint8 *keys = SDL_GetKeyboardState(NULL);
            /* ... existing camera code ... */
        }
```

- [ ] **Step 5: Add console_render call after bf_render**

After `bf_render(engine, pixels);` (line 438), add:

```c
        console_render(&console, pixels, WINDOW_W, WINDOW_H, engine);
```

- [ ] **Step 6: Build and test**

Run: `cd /home/rafa/repos/c && make`
Expected: Clean build.

Then run: `./apps/battleforge/battleforge`
Expected:
- Game runs normally
- Press backtick (`` ` ``) → translucent console slides over top half
- Type `help` + Enter → command list appears in green text
- Type `entity create 10 0 5 0 3 0 0 1 3` + Enter → new entity appears
- Press backtick → console closes, camera controls work again

- [ ] **Step 7: Commit**

```bash
git add apps/battleforge/main.c
git commit -m "feat(battleforge): integrate in-game console into shell"
```

---

### Task 8: Smoke Test and Polish

**Files:**
- Possibly: `apps/battleforge/console.c` (minor fixes)

- [ ] **Step 1: Full integration test**

Run battleforge and verify:
1. Console toggle with backtick works
2. Camera controls suppressed when console is open
3. Mouse clicking suppressed when console is open
4. `help` command lists all commands
5. `entity create 10 0 5 0 3 0 0 1 3` creates an entity
6. `entity move 1 10 0 10` moves entity 1
7. `select 2` selects entity 2
8. `camera set 0 20 20 0 -0.5 -1` repositions camera
9. Engine logs appear (entity created, entity moving, etc.)
10. Invalid command shows error in console
11. Page Up/Down scrolls through log history
12. Cursor movement (Left/Right/Home/End) works in input line

- [ ] **Step 2: Fix any issues found during testing**

- [ ] **Step 3: Final commit if fixes were needed**

```bash
git add -u
git commit -m "fix(battleforge): polish console after smoke testing"
```
