#ifndef BATTLEFORGE_H
#define BATTLEFORGE_H

#include <stdint.h>
#include "vector.h"
#include "raytrace.h"

/* Forward declaration — full definition in slice.h */
typedef struct slice_sheet slice_sheet;

/* --- Configuration --- */

typedef struct {
    int render_width;
    int render_height;
    float fov;
    int num_threads;
} bf_config;

/* --- Map --- */

typedef struct {
    float width;
    float depth;
    uint8_t r, g, b;
    float ambient;
    vector light_dir;
    float light_intensity;
} bf_map;

/* --- Commands --- */

typedef enum {
    BF_CMD_CAMERA_SET,
    BF_CMD_CAMERA_MOVE,
    BF_CMD_ENTITY_CREATE,
    BF_CMD_ENTITY_DESTROY,
    BF_CMD_ENTITY_MOVE,
    BF_CMD_ENTITY_FACE,
    BF_CMD_ENTITY_SET_SPEED,
    BF_CMD_SELECT,
    BF_CMD_ENTITY_ANIMATE,
    BF_CMD_COUNT
} bf_cmd_type;

typedef struct {
    bf_cmd_type type;
    union {
        struct { vector position; vector direction; } camera_set;
        struct { vector delta; } camera_move;
        struct { int id; int sprite_id; vector position; vector direction; float speed; } entity_create;
        struct { int id; } entity_destroy;
        struct { int id; vector position; } entity_move;
        struct { int id; vector direction; } entity_face;
        struct { int id; float speed; } entity_set_speed;
        struct { int id; } select;
        struct { int id; int anim_index; } entity_animate;
    };
} bf_cmd;

/* --- Picking --- */

typedef enum {
    BF_PICK_SKY,
    BF_PICK_GROUND,
    BF_PICK_ENTITY
} bf_pick_type;

typedef struct {
    bf_pick_type type;
    int entity_id;
    vector position;
} bf_pick_result;

/* --- Engine --- */

typedef struct bf_engine bf_engine;

bf_engine  *bf_create(bf_config config);
void        bf_destroy(bf_engine *e);

int         bf_register_sprite(bf_engine *e, slice_sheet *sheet,
                               float world_width, float world_height);
void        bf_set_map(bf_engine *e, bf_map map);

int         bf_command(bf_engine *e, bf_cmd cmd);
void        bf_tick(bf_engine *e, float dt);
void        bf_render(bf_engine *e, uint32_t *pixel_buf);
bf_pick_result bf_pick(bf_engine *e, int screen_x, int screen_y);

#endif /* BATTLEFORGE_H */
