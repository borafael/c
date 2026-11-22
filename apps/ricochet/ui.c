#include "ui.h"
#include "render.h"
#include "sfx.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#define MOUSE_SENS 0.0024f   /* radians per mouse count at speed 10 */

/* --------------------------------------------------------------- Menus */

/* The screens around the game: a title screen (over a quiet bot match),
 * pause and options. menu.c draws them and handles keys and the mouse;
 * what each item does is in on_menu_event in app.c. */

#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

static menu_item title_items[] = {
    [T_START]   = {.kind = MENU_ACTION, .label = "START"},
    [T_SCORES]  = {.kind = MENU_ACTION, .label = "HIGH SCORES"},
    [T_OPTIONS] = {.kind = MENU_ACTION, .label = "OPTIONS"},
    [T_QUIT]    = {.kind = MENU_ACTION, .label = "QUIT"},
};
menu title_menu = {.title = "RICOCHET", .items = title_items,
                   .count = COUNT(title_items),
                   .footer = "ARROWS OR MOUSE - ENTER SELECTS"};

static menu_item pause_items[] = {
    [P_RESUME]  = {.kind = MENU_ACTION, .label = "RESUME"},
    [P_RESTART] = {.kind = MENU_ACTION, .label = "RESTART"},
    [P_OPTIONS] = {.kind = MENU_ACTION, .label = "OPTIONS"},
    [P_TITLE]   = {.kind = MENU_ACTION, .label = "MAIN MENU"},
    [P_QUIT]    = {.kind = MENU_ACTION, .label = "QUIT"},
};
menu pause_menu = {.title = "PAUSED", .items = pause_items,
                   .count = COUNT(pause_items)};

/* Options edit these (and game_cfg.mag, the app's bloom / fullscreen flags)
 * in place; on_menu_event applies whatever needs more than a new value. */
static const char *const OFF_ON[] = {"OFF", "ON"};
static int opt_start_ammo;   /* 0..game_cfg.mag; game_cfg.mag = full (game_cfg.start_ammo -1) */
static int opt_mouse = 10;   /* mouse speed, 10 = MOUSE_SENS */
int opt_reflect = 1;
menu_item option_items[] = {
    [O_MAG]        = {.kind = MENU_VALUE, .label = "MAGAZINE", .value = &game_cfg.mag,
                      .min = 1, .max = AMMO_CAP, .step = 5},
    [O_START]      = {.kind = MENU_VALUE, .label = "START AMMO", .value = &opt_start_ammo,
                      .min = 0, .max = DEFAULT_AMMO, .step = 5, .max_name = "FULL"},
    [O_WAVE]       = {.kind = MENU_VALUE, .label = "START WAVE", .value = &game_cfg.start_wave,
                      .min = 1, .max = MAX_START_WAVE, .step = 1},
    [O_MOUSE]      = {.kind = MENU_VALUE, .label = "MOUSE SPEED", .value = &opt_mouse,
                      .min = 1, .max = 20, .step = 1},
    [O_REFLECT]    = {.kind = MENU_VALUE, .label = "REFLECTIONS", .value = &opt_reflect,
                      .min = 0, .max = 1, .step = 1, .names = OFF_ON},
    [O_BLOOM]      = {.kind = MENU_VALUE, .label = "BLOOM",
                      .min = 0, .max = 1, .step = 1, .names = OFF_ON},
    [O_RENDERER]   = {.kind = MENU_VALUE, .label = "RENDERER"},
    [O_RES]        = {.kind = MENU_VALUE, .label = "RESOLUTION"},
    [O_FULLSCREEN] = {.kind = MENU_VALUE, .label = "FULLSCREEN",
                      .min = 0, .max = 1, .step = 1, .names = OFF_ON},
    [O_BACK]       = {.kind = MENU_ACTION, .label = "BACK"},
};
menu options_menu = {.title = "OPTIONS", .items = option_items,
                     .count = COUNT(option_items)};

float mouse_sens(void) { return MOUSE_SENS * (float)opt_mouse / 10.0f; }

/* Seed the option values from the command line. */
void options_init(void) {
    opt_start_ammo = game_cfg.start_ammo < 0 ? game_cfg.mag : game_cfg.start_ammo;
    opt_reflect = !render_opts.no_reflections;
}

/* Text and ranges that follow live state. render_h == 0 shows NATIVE. */
void options_refresh(int gpu_active, int can_switch, int render_h, int in_game) {
    option_items[O_START].max = game_cfg.mag;
    snprintf(option_items[O_RENDERER].text, sizeof option_items[O_RENDERER].text,
             "%s", gpu_active ? "GPU" : "CPU");
    option_items[O_RENDERER].disabled = !can_switch;
    if (render_h > 0)
        snprintf(option_items[O_RES].text, sizeof option_items[O_RES].text, "%dP", render_h);
    else
        snprintf(option_items[O_RES].text, sizeof option_items[O_RES].text, "NATIVE");
    options_menu.footer = in_game ? "START AMMO AND WAVE APPLY ON RESTART" : "ESC BACK";
}

/* MAGAZINE or START AMMO was stepped: keep --start-ammo consistent. A
 * full start stays full when the magazine grows. */
void options_ammo_changed(int item) {
    if (item == O_MAG && (game_cfg.start_ammo < 0 || opt_start_ammo > game_cfg.mag))
        opt_start_ammo = game_cfg.mag;
    option_items[O_START].max = game_cfg.mag;
    game_cfg.start_ammo = opt_start_ammo >= game_cfg.mag ? -1 : opt_start_ammo;
}

void menu_feedback(menu_event ev) {
    switch (ev.kind) {
    case MENU_MOVED:    sfx_play(SFX_DRY, 0.35f, 0.0f, 1.7f); break;
    case MENU_CHANGED:  sfx_play(SFX_DRY, 0.50f, 0.0f, ev.dir > 0 ? 1.3f : 1.1f); break;
    case MENU_ACTIVATE: sfx_play(SFX_PICKUP, 0.40f, 0.0f, 1.1f); break;
    case MENU_BACK:     sfx_play(SFX_PICKUP, 0.30f, 0.0f, 0.8f); break;
    default: break;
    }
}

/* One table row: "#  SCORE  WAVE KILLS  ACC MAG  DATE". The wave column
 * reads "5-14" for a game that started at wave 5. */
static void score_row(char *buf, size_t n, int rank, const hs_entry *e) {
    char wave[16], acc[8], date[8] = "-";
    if (e->start_wave > 1) snprintf(wave, sizeof wave, "%d-%d", e->start_wave, e->wave);
    else snprintf(wave, sizeof wave, "%d", e->wave);
    snprintf(acc, sizeof acc, "%d%%", e->shots > 0 ? 100 * e->hits / e->shots : 0);
    time_t when = (time_t)e->date;
    struct tm *tm = e->date > 0 ? localtime(&when) : NULL;
    if (tm) strftime(date, sizeof date, "%m/%d", tm);
    snprintf(buf, n, "%2d %8d %5s %5d %4s %3d %5s", rank + 1, e->score, wave, e->kills,
             acc, e->mag, date);
}

void draw_scores(hud_target t, const hs_table *tab, int highlight, float time) {
    int sc = t.h / 150;
    if (sc < 1) sc = 1;
    hud_tint(t, 0x060812u, 0.70f);
    int row_h = 9 * sc;
    int top = (t.h - 27 * sc - (HS_MAX + 1) * row_h - 12 * sc) / 2;
    hud_text_center(t, top + 11 * sc, "HIGH SCORES", sc * 3, 0xFF78F5FFu);
    char head[64], row[64];
    snprintf(head, sizeof head, "%2s %8s %5s %5s %4s %3s %5s", "", "SCORE", "WAVE", "KILLS",
             "ACC", "MAG", "DATE");
    int x = (t.w - hud_text_width(head, sc)) / 2, y = top + 27 * sc;
    hud_blend_rect(t, x - 4 * sc, y - 3 * sc, hud_text_width(head, sc) + 8 * sc,
                   (HS_MAX + 1) * row_h + 4 * sc, 0x000000u, 0.55f);
    hud_text(t, x, y, head, sc, 0xFF8890A8u);
    if (tab->n == 0)
        hud_text_center(t, y + 4 * row_h, "NO SCORES YET - GO PLAY", sc, 0xFFB0B8D0u);
    for (int i = 0; i < tab->n; i++) {
        score_row(row, sizeof row, i, &tab->e[i]);
        uint32_t c = i == 0 ? 0xFFFFE070u : 0xFFB0B8D0u;
        if (i == highlight) {
            float pulse = 0.5f + 0.5f * sinf(time * 6.0f);
            hud_blend_rect(t, x - 4 * sc, y + (i + 1) * row_h - sc, hud_text_width(head, sc) + 8 * sc,
                           row_h, 0x78F5FFu, 0.12f + 0.10f * pulse);
            c = 0xFFFFFFFFu;
        }
        hud_text(t, x, y + (i + 1) * row_h, row, sc, c);
    }
    hud_text_center(t, y + (HS_MAX + 1) * row_h + 8 * sc, "PRESS ANY KEY", sc, 0xFF8890A8u);
}
