#ifndef RICOCHET_GAME_INTERNAL_H
#define RICOCHET_GAME_INTERNAL_H

/* Shared by the two files that make up the game rules, and nobody else
 * (the rest of the app sees only game.h):
 *
 *   game.c   player, firing, the grenade, pickups, the physics world and
 *            what its contacts mean, level build / reset, scoring, the frame
 *   enemy.c  the kind table, waves and spawning, steering, the specials */

#include "game.h"
#include "sfx.h"

/* game.c: sound, feedback and collision helpers */
void   game_sfx(sfx_id id, float gain, float pan, float rate);
void   play_at(game *g, sfx_id id, vector at, float gain, float rate);   /* panned, faded */
void   banner(game *g, const char *s);
void   burst(game *g, vector at, scene_color c, int n, float speed);   /* particles */
/* Push a circle (xz) out of pillars, then boxes. Returns the normal of
 * the last push, or a zero vector if there was no contact. */
vector push_out_static(const game *g, vector *p, float r);
void   clamp_arena(const game *g, vector *p, float r);

/* enemy.c */
float  enemy_size(const enemy *e);        /* radius before the death swell */
void   spawn_wave(game *g);
void   update_enemies(game *g, float dt);
/* A ball hit enemy e hard enough to hurt it: flash, maybe kill and score. */
void   enemy_damage(game *g, enemy *e, vector at);
/* Let n of the balls an enemy holds out. */
void   enemy_release(game *g, enemy *e, int n);
/* Balls enemies shot (game.shot_by) around the physics step: note where
 * they were (returns how many there are), then hurt the player with the
 * ones that went through them. */
int    shots_before(const game *g, vector *prev);
void   shots_after(game *g, const vector *prev);

#endif
