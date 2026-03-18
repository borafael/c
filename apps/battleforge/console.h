#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include "battleforge.h"

#define CONSOLE_INPUT_SIZE 256
#define CONSOLE_MAX_SHELL_MSGS 128
#define CONSOLE_MSG_TEXT_SIZE 256
#define CONSOLE_HEIGHT 150       /* pixels from top of screen (1/4 of 600) */
#define CONSOLE_FONT_W 8
#define CONSOLE_FONT_H 16
#define CONSOLE_HISTORY_SIZE 32
#define CONSOLE_SLIDE_SPEED 1500.0f  /* pixels per second */

typedef struct {
    int open;
    float slide_pos;             /* 0.0 = closed, CONSOLE_HEIGHT = fully open */
    char input[CONSOLE_INPUT_SIZE];
    int input_len;
    int cursor;
    int scroll_offset;

    /* Command history */
    char history[CONSOLE_HISTORY_SIZE][CONSOLE_INPUT_SIZE];
    int history_count;           /* total stored (capped at CONSOLE_HISTORY_SIZE) */
    int history_write_pos;       /* next write slot */
    int history_browse;          /* -1 = not browsing, 0..count-1 = index from newest */
    char input_saved[CONSOLE_INPUT_SIZE]; /* saved input while browsing history */

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
int  console_visible_height(const console_state *cs);
void console_update(console_state *cs, float dt);
void console_handle_key(console_state *cs, int sdl_keycode, int sdl_scancode,
                        bf_engine *engine);
void console_handle_text(console_state *cs, const char *text);
void console_render(const console_state *cs, uint32_t *pixels,
                    int screen_width, int screen_height,
                    const bf_engine *engine);

#endif /* CONSOLE_H */
