#ifndef RICOCHET_HUD_H
#define RICOCHET_HUD_H

#include <stdint.h>

/* Immediate-mode HUD drawing straight into an ARGB framebuffer.
 * Text uses an embedded 5x7 font (A-Z, 0-9, space, and : - / ! . %),
 * scaled by an integer factor, with an optional 1-px drop shadow. */

typedef struct {
    uint32_t *px;
    int w, h;
} hud_target;

int  hud_text_width(const char *s, int scale);
void hud_text(hud_target t, int x, int y, const char *s, int scale, uint32_t argb);
void hud_text_center(hud_target t, int cy, const char *s, int scale, uint32_t argb);
void hud_rect(hud_target t, int x, int y, int w, int h, uint32_t argb);
void hud_blend_rect(hud_target t, int x, int y, int w, int h, uint32_t rgb, float a);
void hud_circle(hud_target t, int cx, int cy, int r, uint32_t argb, int filled);
/* Blend the whole frame toward rgb by a (0..1). */
void hud_tint(hud_target t, uint32_t rgb, float a);

#endif
