#ifndef RICOCHET_RENDER_H
#define RICOCHET_RENDER_H

/* Turns the game into pixels: builds the scene (arena, enemies, balls,
 * particles, the gun viewmodel), raytraces it, runs the post effects and
 * draws the HUD on top. */

#include "game.h"
#include "postfx.h"
#include "renderer.h"
#include "scene.h"

#include <stdint.h>

typedef struct {
    int no_reflections;   /* --no-reflections / options: matte surfaces */
    int no_gun, no_parts; /* --bench: attribute render cost */
} render_options;
extern render_options render_opts;

typedef struct {
    postfx_bloom_ctx *bloom;
    int bloom_on;
} fx_state;

/* Render g from the player's eye into px (w x h ARGB). Also updates the
 * game's listener frame for spatial audio. hud = 0 leaves the HUD off
 * (behind menus). */
void render_frame(game *g, rt_renderer *rnd, scene *s, scene_camera *cam,
                  uint32_t *px, int w, int h, fx_state *fx, int hud);

#endif
