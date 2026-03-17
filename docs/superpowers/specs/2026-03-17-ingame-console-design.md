# In-Game Console Design

## Overview

A Quake-style drop-down console for the battleforge engine providing command input, game log viewing, and debug/developer interaction. Slides down from the top of the screen with a translucent background overlay.

## Architecture

```
Shell (main.c)
  Console UI
  ├─ Toggle state (open/closed)
  ├─ Input line buffer
  ├─ Shell message buffer (parse errors, help text)
  ├─ Read cursor into engine log buffer
  ├─ Bitmap font renderer
  └─ Translucent overlay blitter
  │
  │ bf_command(), bf_log_count(), bf_log_get()
  ▼
Engine (battleforge.h/c)
  Log ring buffer
  ├─ bf_log(engine, level, fmt, ...)
  ├─ bf_log_count(engine)
  ├─ bf_log_get(engine, index)
  └─ Fixed-size circular buffer (512 entries)
```

The console lives entirely in the shell. The engine gains only a log ring buffer and API. The renderer is untouched.

## Engine Log API

### Types

```c
typedef enum {
    BF_LOG_INFO,
    BF_LOG_WARN,
    BF_LOG_ERROR
} bf_log_level;

typedef struct {
    bf_log_level level;
    char text[256];
} bf_log_entry;
```

### Internal State (inside bf_engine)

```c
#define BF_LOG_BUFFER_SIZE 512

bf_log_entry log_buffer[BF_LOG_BUFFER_SIZE];
int log_write_pos;   // Next write position (advances and wraps)
int log_count;       // Total entries stored (capped at BF_LOG_BUFFER_SIZE)
```

Single write index — no head/tail since there's no consumer dequeuing entries. `log_count` tracks how many valid entries exist (grows up to `BF_LOG_BUFFER_SIZE`, then stays there). The oldest surviving entry is at `(log_write_pos - log_count + BF_LOG_BUFFER_SIZE) % BF_LOG_BUFFER_SIZE`.

### Public API (added to battleforge.h)

```c
void bf_log(bf_engine *e, bf_log_level level, const char *fmt, ...);
int bf_log_count(const bf_engine *e);
const bf_log_entry *bf_log_get(const bf_engine *e, int index); // 0 = oldest surviving
```

`bf_log()` formats the message with `vsnprintf` into the entry's text field, advances `log_write_pos`, increments `log_count` (clamped to `BF_LOG_BUFFER_SIZE`).

`bf_log_get(e, i)` translates index to buffer position: `(log_write_pos - log_count + i + BF_LOG_BUFFER_SIZE) % BF_LOG_BUFFER_SIZE`. Returns NULL if `i >= log_count`.

The engine calls `bf_log()` from command handlers and simulation events (entity spawned, command failed, selection changed, etc.).

## Console UI (Shell)

### State

```c
typedef struct {
    int open;
    char input[256];
    int input_len;
    int cursor;
    int scroll_offset;        // 0 = bottom (newest), positive = scrolled up
    // Shell-generated messages (parse errors, help text)
    char shell_messages[128][256];
    int shell_msg_count;      // Total shell messages (ring buffer, wraps at 128)
    int shell_msg_write_pos;
    // Interleaving: each shell message records the engine log_count
    // at the time it was generated, so we know where to insert it
    int shell_msg_log_cursor[128];
} console_state;
```

### Input Handling

Toggle: Backtick (`` ` ``) opens/closes the console. When open, all keyboard input is captured by the console — both `SDL_PollEvent` key events AND `SDL_GetKeyboardState` polled input (camera movement) are suppressed.

When open:
- Printable characters: insert into `input` at `cursor`
- Backspace: delete character behind cursor
- Left/Right arrows: move cursor
- Enter: parse input, execute command, clear input
- Page Up/Down: adjust `scroll_offset` to browse log history
- Backtick: close console

Scroll bounds: `scroll_offset` is clamped between 0 (showing newest messages) and `total_lines - visible_lines` (showing oldest). When `scroll_offset == 0` and new messages arrive, the view auto-scrolls. When scrolled up, new messages do not change the view.

### Command Parsing

Simple verb-args format. Split input on whitespace, match first token(s) against a command table:

```c
{ "entity create",    parse_entity_create    },  // -> BF_CMD_ENTITY_CREATE
{ "entity destroy",   parse_entity_destroy   },  // -> BF_CMD_ENTITY_DESTROY
{ "entity move",      parse_entity_move      },  // -> BF_CMD_ENTITY_MOVE
{ "entity face",      parse_entity_face      },  // -> BF_CMD_ENTITY_FACE
{ "entity speed",     parse_entity_set_speed },  // -> BF_CMD_ENTITY_SET_SPEED
{ "entity animate",   parse_entity_animate   },  // -> BF_CMD_ENTITY_ANIMATE
{ "camera set",       parse_camera_set       },  // -> BF_CMD_CAMERA_SET
{ "camera move",      parse_camera_move      },  // -> BF_CMD_CAMERA_MOVE
{ "select",           parse_select           },  // -> BF_CMD_SELECT
{ "help",             parse_help             },  // prints available commands
```

Each parse function reads remaining args, builds a `bf_cmd`, calls `bf_command()`. On failure, the error message is added to the shell's own message buffer (not `bf_log()`).

### Display Sources

The console screen merges two sources:
- **Engine logs** from `bf_log_count()` / `bf_log_get()` — game events, simulation messages
- **Shell messages** — parse errors, help output, "unknown command"

Each shell message records the engine `log_count` at the time it was created (`shell_msg_log_cursor`). When building the display list, shell messages are inserted at the point where the engine log count matches their cursor value, producing chronological interleaving.

### Rendering

Happens after `bf_render()` writes the pixel buffer, before SDL presentation.

Console covers the top half of the screen (300px at 800x600). With 16px row height, this fits 18 lines of text plus the input line.

1. If console is closed, skip
2. Alpha-blend a dark tint over the top 300 pixels of the pixel buffer. Integer blend per channel: `((pixel_ch * 77) + (tint_ch * 179)) >> 8` (approximately 30% scene + 70% tint)
3. Blit log/shell messages bottom-to-top from the input line, respecting `scroll_offset`
4. Blit input line at the bottom of the console area with a `>` prompt and blinking cursor

### Bitmap Font

8x16 monospace font stored as a PNG spritesheet at `apps/battleforge/assets/font.png`. 128 ASCII glyphs laid out in a grid (16 columns x 8 rows). Loaded with `stb_image` (already used by the slice library). To render character `c`:
- Column: `c % 16`
- Row: `c / 16`
- Copy the 8x16 pixel rectangle from the atlas into the pixel buffer

If the font PNG fails to load, the console is disabled (stays closed, backtick does nothing). A warning is printed to stderr.

## Transparency

Alpha blending is done in software on the pixel buffer after raytracing completes. Per-pixel integer blend over the console's rectangular region. Cheap — single pass, no extra rays, no scene changes.

## What This Design Does Not Include

- Cvars / persistent variable registry (can be added later as a `set` command)
- Shell-side scrollback copy (engine ring buffer is the single source of truth)
- Autocomplete (future enhancement)
- Command history with up/down arrows (future enhancement)
- Console slide animation (future enhancement — starts as instant toggle)
