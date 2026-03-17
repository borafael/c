#include "console.h"
#include "stb_image.h"    /* declarations only -- implementation lives in slice.c */
#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

/* --- Static forward declarations --- */

static void console_execute(console_state *cs, bf_engine *engine);

/* --- Init / Destroy --- */

int console_init(console_state *cs, int screen_width, int screen_height,
                 const char *font_path)
{
    memset(cs, 0, sizeof(*cs));
    cs->screen_width = screen_width;
    cs->screen_height = screen_height;

    int w, h, channels;
    unsigned char *data = stbi_load(font_path, &w, &h, &channels, 4);
    if (!data) {
        fprintf(stderr, "console_init: failed to load font '%s'\n", font_path);
        return -1;
    }
    if (w != 128 || h != 128) {
        fprintf(stderr, "console_init: font must be 128x128, got %dx%d\n", w, h);
        stbi_image_free(data);
        return -1;
    }
    cs->font_pixels = (uint32_t *)data;
    cs->font_loaded = 1;
    return 0;
}

void console_destroy(console_state *cs)
{
    if (cs->font_pixels) {
        stbi_image_free(cs->font_pixels);
        cs->font_pixels = NULL;
    }
    cs->font_loaded = 0;
}

/* --- Toggle / Query --- */

void console_toggle(console_state *cs)
{
    if (!cs->font_loaded) return;
    cs->open = !cs->open;
}

int console_is_open(const console_state *cs)
{
    return cs->open;
}

/* --- Shell messages --- */

static void console_shell_msg(console_state *cs, const bf_engine *engine,
                               const char *fmt, ...)
{
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

/* --- Drawing helpers --- */

static void console_draw_overlay(uint32_t *pixels, int screen_width,
                                  int console_height)
{
    for (int i = 0; i < screen_width * console_height; i++) {
        uint32_t p = pixels[i];
        uint8_t r = (((p >> 16) & 0xFF) * 77) >> 8;
        uint8_t g = (((p >> 8) & 0xFF) * 77) >> 8;
        uint8_t b = ((p & 0xFF) * 77) >> 8;
        pixels[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

static void console_draw_char(const console_state *cs, uint32_t *pixels,
                               int screen_width, int screen_height,
                               int x, int y, unsigned char c, uint32_t color)
{
    if (!cs->font_pixels) return;
    if (c > 127) c = '?';

    int col = c % 16;
    int row = c / 16;
    int gx = col * CONSOLE_FONT_W;
    int gy = row * CONSOLE_FONT_H;

    for (int py = 0; py < CONSOLE_FONT_H; py++) {
        int dy = y + py;
        if (dy < 0 || dy >= screen_height) continue;
        for (int px = 0; px < CONSOLE_FONT_W; px++) {
            int dx = x + px;
            if (dx < 0 || dx >= screen_width) continue;
            uint32_t fp = cs->font_pixels[(gy + py) * 128 + (gx + px)];
            uint8_t alpha = (fp >> 24) & 0xFF;
            if (alpha > 128) {
                pixels[dy * screen_width + dx] = color;
            }
        }
    }
}

static void console_draw_string(const console_state *cs, uint32_t *pixels,
                                 int screen_width, int screen_height,
                                 int x, int y, const char *str, uint32_t color)
{
    while (*str) {
        console_draw_char(cs, pixels, screen_width, screen_height,
                          x, y, (unsigned char)*str, color);
        x += CONSOLE_FONT_W;
        str++;
    }
}

/* --- Render --- */

void console_render(const console_state *cs, uint32_t *pixels,
                    int screen_width, int screen_height,
                    const bf_engine *engine)
{
    if (!cs->open || !cs->font_loaded) return;

    int console_height = CONSOLE_HEIGHT;
    if (console_height > screen_height) console_height = screen_height;

    console_draw_overlay(pixels, screen_width, console_height);

    uint32_t text_color  = 0xFF00FF00u;  /* green */
    uint32_t error_color = 0xFFFF4444u;
    uint32_t warn_color  = 0xFFFFFF00u;

    /* Draw input line at bottom of console area */
    int input_y = console_height - CONSOLE_FONT_H - 2;
    console_draw_string(cs, pixels, screen_width, screen_height,
                        4, input_y, "> ", text_color);
    console_draw_string(cs, pixels, screen_width, screen_height,
                        4 + 2 * CONSOLE_FONT_W, input_y, cs->input, text_color);

    /* Draw cursor */
    int cursor_x = 4 + (2 + cs->cursor) * CONSOLE_FONT_W;
    console_draw_char(cs, pixels, screen_width, screen_height,
                      cursor_x, input_y, '_', text_color);

    /* Draw separator line */
    int sep_y = input_y - 2;
    if (sep_y >= 0 && sep_y < screen_height) {
        for (int x = 0; x < screen_width; x++)
            pixels[sep_y * screen_width + x] = 0xFF004400u;
    }

    /* Merge engine logs + shell messages chronologically */
    int log_count = bf_log_count(engine);
    int shell_count = cs->shell_msg_count;
    int total = log_count + shell_count;

    if (total == 0) return;

    /* Temporary merged display array */
    typedef struct {
        const char *text;
        uint32_t color;
    } display_line;

    display_line *lines = (display_line *)malloc((size_t)total * sizeof(display_line));
    if (!lines) return;

    int line_count = 0;

    /* Build merged list: walk through engine logs and insert shell messages
       at their recorded log_cursor positions */
    int shell_idx_start = 0;
    if (cs->shell_msg_count < CONSOLE_MAX_SHELL_MSGS) {
        shell_idx_start = 0;
    } else {
        shell_idx_start = cs->shell_msg_write_pos; /* oldest entry */
    }

    int shell_remaining = shell_count;
    int next_shell = 0; /* index into ordered shell messages */
    int log_idx = 0;

    /* Pre-build ordered shell message indices */
    int *shell_order = NULL;
    if (shell_count > 0) {
        shell_order = (int *)malloc((size_t)shell_count * sizeof(int));
        if (!shell_order) {
            free(lines);
            return;
        }
        for (int i = 0; i < shell_count; i++) {
            shell_order[i] = (shell_idx_start + i) % CONSOLE_MAX_SHELL_MSGS;
        }
    }

    /* Merge: for each log position, insert shell messages that belong before it */
    while (log_idx < log_count || next_shell < shell_count) {
        /* Insert shell messages whose log_cursor <= log_idx */
        while (next_shell < shell_count) {
            int si = shell_order[next_shell];
            if (cs->shell_msg_log_cursor[si] <= log_idx) {
                lines[line_count].text = cs->shell_messages[si];
                lines[line_count].color = text_color;
                line_count++;
                next_shell++;
            } else {
                break;
            }
        }
        /* Insert the current engine log entry */
        if (log_idx < log_count) {
            const bf_log_entry *entry = bf_log_get(engine, log_idx);
            if (entry) {
                uint32_t c = text_color;
                if (entry->level == BF_LOG_ERROR) c = error_color;
                else if (entry->level == BF_LOG_WARN) c = warn_color;
                lines[line_count].text = entry->text;
                lines[line_count].color = c;
                line_count++;
            }
            log_idx++;
        }
    }

    /* Render visible lines bottom-to-top respecting scroll_offset */
    int visible_lines = (sep_y - 2) / CONSOLE_FONT_H;
    int start = line_count - 1 - cs->scroll_offset;

    for (int i = 0; i < visible_lines && start - i >= 0; i++) {
        int li = start - i;
        int y = sep_y - 4 - (i + 1) * CONSOLE_FONT_H;
        if (y < 0) break;
        console_draw_string(cs, pixels, screen_width, screen_height,
                            4, y, lines[li].text, lines[li].color);
    }

    free(shell_order);
    free(lines);
}

/* --- Text Input --- */

void console_handle_text(console_state *cs, const char *text)
{
    while (*text) {
        if (cs->input_len >= CONSOLE_INPUT_SIZE - 1) break;
        unsigned char c = (unsigned char)*text;
        if (isprint(c)) {
            /* Shift chars right to make room at cursor */
            memmove(&cs->input[cs->cursor + 1],
                    &cs->input[cs->cursor],
                    (size_t)(cs->input_len - cs->cursor));
            cs->input[cs->cursor] = (char)c;
            cs->input_len++;
            cs->cursor++;
            cs->input[cs->input_len] = '\0';
        }
        text++;
    }
}

/* --- Key Handling --- */

void console_handle_key(console_state *cs, int sdl_keycode, int sdl_scancode,
                        bf_engine *engine)
{
    (void)sdl_scancode;

    switch (sdl_keycode) {
    case SDLK_BACKSPACE:
        if (cs->cursor > 0) {
            memmove(&cs->input[cs->cursor - 1],
                    &cs->input[cs->cursor],
                    (size_t)(cs->input_len - cs->cursor));
            cs->cursor--;
            cs->input_len--;
            cs->input[cs->input_len] = '\0';
        }
        break;

    case SDLK_DELETE:
        if (cs->cursor < cs->input_len) {
            memmove(&cs->input[cs->cursor],
                    &cs->input[cs->cursor + 1],
                    (size_t)(cs->input_len - cs->cursor - 1));
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
        }
        cs->input[0] = '\0';
        cs->input_len = 0;
        cs->cursor = 0;
        cs->scroll_offset = 0;
        break;

    case SDLK_PAGEUP:
        cs->scroll_offset += 5;
        /* Clamp will happen naturally in render */
        break;

    case SDLK_PAGEDOWN:
        cs->scroll_offset -= 5;
        if (cs->scroll_offset < 0) cs->scroll_offset = 0;
        break;
    }
}

/* --- Command parsing helpers --- */

static const char *skip_ws(const char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static int parse_float(const char **s, float *out)
{
    const char *p = skip_ws(*s);
    char *end;
    float val = strtof(p, &end);
    if (end == p) return -1;
    *out = val;
    *s = end;
    return 0;
}

static int parse_int(const char **s, int *out)
{
    const char *p = skip_ws(*s);
    char *end;
    long val = strtol(p, &end, 10);
    if (end == p) return -1;
    *out = (int)val;
    *s = end;
    return 0;
}

static const char *match_prefix(const char *input, const char *prefix)
{
    while (*prefix) {
        if (tolower((unsigned char)*input) != tolower((unsigned char)*prefix))
            return NULL;
        input++;
        prefix++;
    }
    /* Must be followed by whitespace, end-of-string, or another token separator */
    if (*input && !isspace((unsigned char)*input))
        return NULL;
    return skip_ws(input);
}

/* --- Individual command handlers --- */

static void cmd_help(console_state *cs, bf_engine *engine, const char *args)
{
    (void)args;
    console_shell_msg(cs, engine, "Available commands:");
    console_shell_msg(cs, engine, "  help                     - show this help");
    console_shell_msg(cs, engine, "  entity create id spr x y z dx dy dz speed");
    console_shell_msg(cs, engine, "  entity destroy id");
    console_shell_msg(cs, engine, "  entity move id x y z");
    console_shell_msg(cs, engine, "  entity face id dx dy dz");
    console_shell_msg(cs, engine, "  entity speed id speed");
    console_shell_msg(cs, engine, "  entity animate id anim_index");
    console_shell_msg(cs, engine, "  camera set x y z dx dy dz");
    console_shell_msg(cs, engine, "  camera move dx dy dz");
    console_shell_msg(cs, engine, "  select id");
}

static void cmd_entity_create(console_state *cs, bf_engine *engine, const char *args)
{
    int id, sprite_id;
    float x, y, z, dx, dy, dz, speed;
    const char *p = args;

    if (parse_int(&p, &id) || parse_int(&p, &sprite_id) ||
        parse_float(&p, &x) || parse_float(&p, &y) || parse_float(&p, &z) ||
        parse_float(&p, &dx) || parse_float(&p, &dy) || parse_float(&p, &dz) ||
        parse_float(&p, &speed)) {
        console_shell_msg(cs, engine,
            "Usage: entity create id sprite_id x y z dx dy dz speed");
        return;
    }

    bf_cmd cmd = {
        .type = BF_CMD_ENTITY_CREATE,
        .entity_create = {
            .id = id, .sprite_id = sprite_id,
            .position = {x, y, z},
            .direction = {dx, dy, dz},
            .speed = speed
        }
    };
    bf_command(engine, cmd);
    console_shell_msg(cs, engine, "Entity %d created.", id);
}

static void cmd_entity_destroy(console_state *cs, bf_engine *engine, const char *args)
{
    int id;
    const char *p = args;
    if (parse_int(&p, &id)) {
        console_shell_msg(cs, engine, "Usage: entity destroy id");
        return;
    }
    bf_command(engine, (bf_cmd){ .type = BF_CMD_ENTITY_DESTROY, .entity_destroy = { .id = id } });
    console_shell_msg(cs, engine, "Entity %d destroyed.", id);
}

static void cmd_entity_move(console_state *cs, bf_engine *engine, const char *args)
{
    int id;
    float x, y, z;
    const char *p = args;
    if (parse_int(&p, &id) || parse_float(&p, &x) || parse_float(&p, &y) || parse_float(&p, &z)) {
        console_shell_msg(cs, engine, "Usage: entity move id x y z");
        return;
    }
    bf_command(engine, (bf_cmd){ .type = BF_CMD_ENTITY_MOVE,
        .entity_move = { .id = id, .position = {x, y, z} } });
    console_shell_msg(cs, engine, "Entity %d moving to (%.1f, %.1f, %.1f).", id, x, y, z);
}

static void cmd_entity_face(console_state *cs, bf_engine *engine, const char *args)
{
    int id;
    float dx, dy, dz;
    const char *p = args;
    if (parse_int(&p, &id) || parse_float(&p, &dx) || parse_float(&p, &dy) || parse_float(&p, &dz)) {
        console_shell_msg(cs, engine, "Usage: entity face id dx dy dz");
        return;
    }
    bf_command(engine, (bf_cmd){ .type = BF_CMD_ENTITY_FACE,
        .entity_face = { .id = id, .direction = {dx, dy, dz} } });
    console_shell_msg(cs, engine, "Entity %d facing (%.1f, %.1f, %.1f).", id, dx, dy, dz);
}

static void cmd_entity_speed(console_state *cs, bf_engine *engine, const char *args)
{
    int id;
    float speed;
    const char *p = args;
    if (parse_int(&p, &id) || parse_float(&p, &speed)) {
        console_shell_msg(cs, engine, "Usage: entity speed id speed");
        return;
    }
    bf_command(engine, (bf_cmd){ .type = BF_CMD_ENTITY_SET_SPEED,
        .entity_set_speed = { .id = id, .speed = speed } });
    console_shell_msg(cs, engine, "Entity %d speed set to %.1f.", id, speed);
}

static void cmd_entity_animate(console_state *cs, bf_engine *engine, const char *args)
{
    int id, anim_index;
    const char *p = args;
    if (parse_int(&p, &id) || parse_int(&p, &anim_index)) {
        console_shell_msg(cs, engine, "Usage: entity animate id anim_index");
        return;
    }
    bf_command(engine, (bf_cmd){ .type = BF_CMD_ENTITY_ANIMATE,
        .entity_animate = { .id = id, .anim_index = anim_index } });
    console_shell_msg(cs, engine, "Entity %d animate %d.", id, anim_index);
}

static void cmd_camera_set(console_state *cs, bf_engine *engine, const char *args)
{
    float x, y, z, dx, dy, dz;
    const char *p = args;
    if (parse_float(&p, &x) || parse_float(&p, &y) || parse_float(&p, &z) ||
        parse_float(&p, &dx) || parse_float(&p, &dy) || parse_float(&p, &dz)) {
        console_shell_msg(cs, engine, "Usage: camera set x y z dx dy dz");
        return;
    }
    bf_command(engine, (bf_cmd){ .type = BF_CMD_CAMERA_SET,
        .camera_set = { .position = {x, y, z}, .direction = {dx, dy, dz} } });
    console_shell_msg(cs, engine, "Camera set to (%.1f, %.1f, %.1f).", x, y, z);
}

static void cmd_camera_move(console_state *cs, bf_engine *engine, const char *args)
{
    float dx, dy, dz;
    const char *p = args;
    if (parse_float(&p, &dx) || parse_float(&p, &dy) || parse_float(&p, &dz)) {
        console_shell_msg(cs, engine, "Usage: camera move dx dy dz");
        return;
    }
    bf_command(engine, (bf_cmd){ .type = BF_CMD_CAMERA_MOVE,
        .camera_move = { .delta = {dx, dy, dz} } });
    console_shell_msg(cs, engine, "Camera moved by (%.1f, %.1f, %.1f).", dx, dy, dz);
}

static void cmd_select(console_state *cs, bf_engine *engine, const char *args)
{
    int id;
    const char *p = args;
    if (parse_int(&p, &id)) {
        console_shell_msg(cs, engine, "Usage: select id");
        return;
    }
    bf_command(engine, (bf_cmd){ .type = BF_CMD_SELECT, .select = { .id = id } });
    console_shell_msg(cs, engine, "Selected entity %d.", id);
}

/* --- Main command dispatcher --- */

static void console_execute(console_state *cs, bf_engine *engine)
{
    const char *input = skip_ws(cs->input);
    if (*input == '\0') return;

    const char *rest;

    /* help */
    rest = match_prefix(input, "help");
    if (rest) { cmd_help(cs, engine, rest); return; }

    /* select */
    rest = match_prefix(input, "select");
    if (rest) { cmd_select(cs, engine, rest); return; }

    /* entity sub-commands */
    rest = match_prefix(input, "entity");
    if (rest) {
        const char *sub;
        sub = match_prefix(rest, "create");
        if (sub) { cmd_entity_create(cs, engine, sub); return; }
        sub = match_prefix(rest, "destroy");
        if (sub) { cmd_entity_destroy(cs, engine, sub); return; }
        sub = match_prefix(rest, "move");
        if (sub) { cmd_entity_move(cs, engine, sub); return; }
        sub = match_prefix(rest, "face");
        if (sub) { cmd_entity_face(cs, engine, sub); return; }
        sub = match_prefix(rest, "speed");
        if (sub) { cmd_entity_speed(cs, engine, sub); return; }
        sub = match_prefix(rest, "animate");
        if (sub) { cmd_entity_animate(cs, engine, sub); return; }

        console_shell_msg(cs, engine,
            "Unknown entity command. Try: create, destroy, move, face, speed, animate");
        return;
    }

    /* camera sub-commands */
    rest = match_prefix(input, "camera");
    if (rest) {
        const char *sub;
        sub = match_prefix(rest, "set");
        if (sub) { cmd_camera_set(cs, engine, sub); return; }
        sub = match_prefix(rest, "move");
        if (sub) { cmd_camera_move(cs, engine, sub); return; }

        console_shell_msg(cs, engine,
            "Unknown camera command. Try: set, move");
        return;
    }

    console_shell_msg(cs, engine,
        "Unknown command: '%s'. Type 'help' for available commands.", input);
}
