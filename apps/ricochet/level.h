#ifndef RICOCHET_LEVEL_H
#define RICOCHET_LEVEL_H

/* A level: the floor size, the outer walls, and the static things on it
 * (pillars, wall segments, crates), plus where the player and enemies
 * start. Pure geometry: the game decides how it bounces and the renderer
 * how it looks.
 *
 * Levels are text files (levels/NAME.lvl), one command per line, '#' starts
 * a comment:
 *
 *   name   Warehouse
 *   size   48 32            floor in metres; outer walls at +-24, +-16
 *   player 0 -3 [yaw]       start, yaw in degrees (0 = facing +z)
 *   pillar x z r            round pillar
 *   box    x0 z0 x1 z1 h    wall segment / crate from the floor, h tall
 *   spawn  x z              enemy spawn point (none: anywhere far away)
 *   wall_height 2.6         outer and '#' walls (also: wall_thickness,
 *                           pillar_height, crate_height, tile)
 *   map                     ASCII layout until 'end'; top row is +z,
 *   ...                     one character per `tile` metres:
 *   end                       #  wall    =  crate   O  pillar
 *                             P  player  E  spawn   .  floor
 *
 * A map sets the floor size from its width and height unless `size` is
 * given; a border of '#' all around is the outer wall and is dropped.
 * Runs of '#' and '=' merge into as few boxes as possible. */

#include "vector.h"

#define LEVEL_MAX_PILLARS 64
#define LEVEL_MAX_BOXES   512
#define LEVEL_MAX_SPAWNS  64

typedef struct { float x, z, r; } level_pillar;
typedef struct { vector min, max; } level_box;

typedef struct {
    char   name[32];
    float  half_x, half_z;        /* outer walls at +-half */
    float  wall_height, wall_thickness, pillar_height;
    vector player;                /* y ignored */
    float  player_yaw;            /* radians */
    int    n_pillars, n_boxes, n_spawns;
    level_pillar pillars[LEVEL_MAX_PILLARS];
    level_box    boxes[LEVEL_MAX_BOXES];
    vector       spawns[LEVEL_MAX_SPAWNS];
} level;

/* The original arena, built in so the game runs without level files. */
void level_builtin_arena(level *lv);

/* Parse `path` into lv. Returns 0, or -1 with a message such as
 * "levels/x.lvl:12: unknown command 'pilar'" in err. */
int level_load(level *lv, const char *path, char *err, int err_len);

/* Same, from text in memory (`origin` names it in error messages). */
int level_parse(level *lv, const char *text, const char *origin, char *err, int err_len);

/* Is the segment a-b (seen from above) clear of every box at least
 * min_height tall? Pillars don't count: they are thin enough to slide
 * around, and leaving them out keeps the arena's behaviour unchanged. */
int level_clear_line(const level *lv, vector a, vector b, float min_height);

#endif
