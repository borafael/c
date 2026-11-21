#include "menu.h"

#include <math.h>
#include <stdio.h>

#define COL_TITLE    0xFF78F5FFu
#define COL_ITEM     0xFFB0B8D0u
#define COL_SEL      0xFFFFFFFFu
#define COL_VALUE    0xFFFFE070u
#define COL_DISABLED 0xFF505868u
#define COL_FOOTER   0xFF8890A8u

static int selectable(const menu *m, int i) {
    return i >= 0 && i < m->count && !m->items[i].disabled;
}

/* Next selectable item from `from` in direction dir, wrapping. */
static int step_sel(const menu *m, int from, int dir) {
    for (int n = 1; n <= m->count; n++) {
        int i = ((from + dir * n) % m->count + m->count) % m->count;
        if (selectable(m, i)) return i;
    }
    return from;
}

static menu_event change(menu *m, int i, int dir) {
    menu_item *it = &m->items[i];
    if (it->kind != MENU_VALUE || it->disabled) return (menu_event){MENU_NONE, i, 0};
    if (it->value) {
        int step = it->step > 0 ? it->step : 1;
        int v = *it->value + dir * step;
        /* Off-grid values (e.g. --ammo 37) snap to the grid first. */
        if (*it->value % step) v = dir > 0 ? (*it->value / step + 1) * step
                                           : (*it->value / step) * step;
        if (v < it->min) v = it->min;
        if (v > it->max) v = it->max;
        if (v == *it->value) return (menu_event){MENU_NONE, i, 0};
        *it->value = v;
    }
    return (menu_event){MENU_CHANGED, i, dir};
}

menu_event menu_on_key(menu *m, menu_key key) {
    if (!selectable(m, m->sel)) m->sel = step_sel(m, m->sel, 1);
    switch (key) {
    case MENU_KEY_UP:
    case MENU_KEY_DOWN: {
        int s = step_sel(m, m->sel, key == MENU_KEY_UP ? -1 : 1);
        if (s == m->sel) return (menu_event){MENU_NONE, s, 0};
        m->sel = s;
        return (menu_event){MENU_MOVED, s, 0};
    }
    case MENU_KEY_LEFT:  return change(m, m->sel, -1);
    case MENU_KEY_RIGHT: return change(m, m->sel, +1);
    case MENU_KEY_OK:
        if (!selectable(m, m->sel)) break;
        if (m->items[m->sel].kind == MENU_VALUE) return change(m, m->sel, +1);
        return (menu_event){MENU_ACTIVATE, m->sel, 0};
    case MENU_KEY_BACK:
        return (menu_event){MENU_BACK, -1, 0};
    }
    return (menu_event){MENU_NONE, m->sel, 0};
}

static int hit(const menu *m, int x, int y) {
    if (m->row_h <= 0 || x < m->x0 || x >= m->x1 || y < m->y0) return -1;
    int i = (y - m->y0) / m->row_h;
    return i < m->count ? i : -1;
}

menu_event menu_on_motion(menu *m, int x, int y) {
    int i = hit(m, x, y);
    if (!selectable(m, i) || i == m->sel) return (menu_event){MENU_NONE, i, 0};
    m->sel = i;
    return (menu_event){MENU_MOVED, i, 0};
}

menu_event menu_on_click(menu *m, int x, int y, int button) {
    int i = hit(m, x, y);
    if (button == 3) return i < 0 ? (menu_event){MENU_BACK, -1, 0}
                                  : (i == m->sel ? change(m, i, -1) : (menu_event){MENU_NONE, i, 0});
    if (button != 1 || !selectable(m, i)) return (menu_event){MENU_NONE, i, 0};
    m->sel = i;
    if (m->items[i].kind == MENU_ACTION) return (menu_event){MENU_ACTIVATE, i, 0};
    /* On the value: left half steps down, right half up; on the label: up. */
    int mid = (m->value_x + m->x1) / 2;
    return change(m, i, x >= m->value_x && x < mid ? -1 : +1);
}

menu_event menu_on_wheel(menu *m, int x, int y, int dy) {
    int i = hit(m, x, y);
    if (!selectable(m, i) || dy == 0) return (menu_event){MENU_NONE, i, 0};
    m->sel = i;
    return change(m, i, dy > 0 ? +1 : -1);
}

static void value_text(const menu_item *it, char *buf, int n) {
    if (!it->value) snprintf(buf, (size_t)n, "%s", it->text);
    else if (it->max_name && *it->value >= it->max) snprintf(buf, (size_t)n, "%s", it->max_name);
    else if (it->names) snprintf(buf, (size_t)n, "%s", it->names[*it->value - it->min]);
    else snprintf(buf, (size_t)n, "%d", *it->value);
}

void menu_draw(menu *m, hud_target t, float time) {
    int sc = t.h / 150;
    if (sc < 1) sc = 1;
    if (!selectable(m, m->sel)) m->sel = step_sel(m, m->sel, 1);

    hud_tint(t, 0x060812u, 0.60f);

    int row_h = 9 * sc;
    int pw = 180 * sc;
    if (pw > t.w - 8 * sc) pw = t.w - 8 * sc;
    int title_h = m->title ? 27 * sc : 0;
    int footer_h = m->footer ? 12 * sc : 0;
    int list_h = m->count * row_h;
    int top = (t.h - title_h - list_h - footer_h) / 2;
    if (top < 4 * sc) top = 4 * sc;
    int x0 = (t.w - pw) / 2, x1 = x0 + pw;

    if (m->title) hud_text_center(t, top + 11 * sc, m->title, sc * 3, COL_TITLE);
    int y0 = top + title_h;
    m->x0 = x0; m->x1 = x1; m->y0 = y0; m->row_h = row_h;
    m->value_x = x0 + pw / 2;

    hud_blend_rect(t, x0, y0 - 3 * sc, pw, list_h + 6 * sc, 0x000000u, 0.55f);
    float pulse = 0.5f + 0.5f * sinf(time * 6.0f);

    for (int i = 0; i < m->count; i++) {
        const menu_item *it = &m->items[i];
        int y = y0 + i * row_h, ty = y + (row_h - 7 * sc) / 2;
        int sel = i == m->sel && !it->disabled;
        uint32_t c = it->disabled ? COL_DISABLED : (sel ? COL_SEL : COL_ITEM);
        if (sel) {
            hud_blend_rect(t, x0, y, pw, row_h, 0x78F5FFu, 0.10f + 0.08f * pulse);
            hud_rect(t, x0, y, sc, row_h, COL_TITLE);
        }
        if (it->kind == MENU_ACTION) {
            hud_text(t, (t.w - hud_text_width(it->label, sc)) / 2, ty, it->label, sc, c);
            continue;
        }
        hud_text(t, x0 + 6 * sc, ty, it->label, sc, c);
        char v[32], shown[40];
        value_text(it, v, sizeof v);
        if (sel) snprintf(shown, sizeof shown, "< %s >", v);
        else snprintf(shown, sizeof shown, "%s", v);
        uint32_t vc = it->disabled ? COL_DISABLED : (sel ? COL_VALUE : COL_ITEM);
        int vx = (m->value_x + x1 - hud_text_width(shown, sc)) / 2;
        hud_text(t, vx, ty, shown, sc, vc);
    }

    if (m->footer)
        hud_text_center(t, y0 + list_h + 9 * sc, m->footer, sc, COL_FOOTER);
}
