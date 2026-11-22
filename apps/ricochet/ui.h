#ifndef RICOCHET_UI_H
#define RICOCHET_UI_H

/* The menus' contents (title, pause, options) and the high-score screen.
 * menu.c is the generic widget; app.c decides what each item does. */

#include "game.h"
#include "highscore.h"
#include "hud.h"
#include "menu.h"

typedef enum { SCREEN_PLAY, SCREEN_TITLE, SCREEN_PAUSE, SCREEN_OPTIONS, SCREEN_SCORES } screen_id;

enum { T_START, T_SCORES, T_OPTIONS, T_QUIT };
enum { P_RESUME, P_RESTART, P_OPTIONS, P_TITLE, P_QUIT };
enum { O_MAG, O_START, O_WAVE, O_MOUSE, O_REFLECT, O_BLOOM, O_RENDERER, O_RES,
       O_FULLSCREEN, O_BACK };

extern menu title_menu, pause_menu, options_menu;
extern menu_item option_items[];
extern int opt_reflect;

/* Mouse-look radians per mouse count (options' MOUSE SPEED). */
float mouse_sens(void);
/* Seed the option values from the command line. */
void options_init(void);
/* Text and ranges that follow live state. render_h == 0 shows NATIVE. */
void options_refresh(int gpu_active, int can_switch, int render_h, int in_game);
/* MAGAZINE or START AMMO was stepped: keep start ammo consistent. */
void options_ammo_changed(int item);
/* Click / blip for a menu event. */
void menu_feedback(menu_event ev);
void draw_scores(hud_target t, const hs_table *tab, int highlight, float time);

#endif
