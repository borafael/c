#ifndef RICOCHET_MENU_H
#define RICOCHET_MENU_H

#include "hud.h"

/* A vertical list menu drawn with the HUD font, driven by keyboard or
 * mouse. It knows nothing about the game: it draws items, moves the
 * selection, adjusts the values it was pointed at, and reports what
 * happened as a menu_event for the caller to act on.
 *
 * Items:
 *   action  label only ("START"); Enter / click -> MENU_ACTIVATE
 *   value   label + value shown on the right ("MAGAZINE   < 40 >").
 *           With `value` set, left / right (or clicking the value's
 *           left / right half, or the wheel) steps *value by `step`
 *           inside [min, max] and reports MENU_CHANGED. With value ==
 *           NULL the menu only reports MENU_CHANGED with the direction
 *           and shows `text`, so the caller can step anything itself
 *           (a resolution ladder, a renderer that may be missing). */

typedef enum { MENU_ACTION, MENU_VALUE } menu_item_kind;

typedef struct {
    menu_item_kind kind;
    const char *label;
    int  *value;                 /* MENU_VALUE: the int it edits, or NULL */
    int   min, max, step;
    const char *const *names;    /* Optional: names[v - min] instead of a number */
    const char *max_name;        /* Optional: shown instead of max ("FULL") */
    char  text[24];              /* Shown when value == NULL */
    int   disabled;              /* Greyed out and skipped */
} menu_item;

typedef struct {
    const char *title;
    menu_item  *items;
    int         count;
    int         sel;
    const char *footer;          /* Optional note under the list */
    /* Layout of the last menu_draw, for mouse hit-testing. */
    int x0, x1, y0, row_h, value_x;
} menu;

typedef enum {
    MENU_NONE,
    MENU_MOVED,      /* selection changed (play a tick) */
    MENU_ACTIVATE,   /* item activated */
    MENU_CHANGED,    /* item's value stepped by dir */
    MENU_BACK,       /* Escape / right click outside */
} menu_event_kind;

typedef struct {
    menu_event_kind kind;
    int item;        /* index into items (-1 for MENU_BACK) */
    int dir;         /* MENU_CHANGED: -1 or +1 */
} menu_event;

typedef enum {
    MENU_KEY_UP, MENU_KEY_DOWN, MENU_KEY_LEFT, MENU_KEY_RIGHT,
    MENU_KEY_OK, MENU_KEY_BACK,
} menu_key;

menu_event menu_on_key(menu *m, menu_key key);
/* Mouse at (x, y) in framebuffer pixels: hovering selects. */
menu_event menu_on_motion(menu *m, int x, int y);
/* button 1 = left, 3 = right. */
menu_event menu_on_click(menu *m, int x, int y, int button);
/* Wheel over a value item steps it (dy > 0 = up = +1). */
menu_event menu_on_wheel(menu *m, int x, int y, int dy);

/* Darken the frame and draw the menu centred on it. `t` (seconds) drives
 * the selection pulse. */
void menu_draw(menu *m, hud_target t, float time);

#endif
