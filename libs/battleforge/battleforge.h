#ifndef BATTLEFORGE_H
#define BATTLEFORGE_H

#include <stdint.h>
#include "vector.h"
#include "raytrace.h"

/* --- Configuration --- */

typedef struct {
    int render_width;
    int render_height;
    float fov;            /* field of view in radians */
    int num_threads;      /* 0 = auto-detect via sysconf */
} bf_config;

/* --- Sprite definition --- */

typedef struct {
    float width;          /* world-space quad width */
    float height;         /* world-space quad height */
    int frame_count;      /* number of viewing angles */
    rt_frame *frames;     /* one frame per angle, clockwise from front */
} bf_sprite_def;

/* --- Map --- */

typedef struct {
    float width;          /* world units */
    float depth;          /* world units */
    uint8_t r, g, b;     /* ground color */
    float ambient;        /* ambient light level (0-1) */
    vector light_dir;     /* directional light direction */
    float light_intensity;/* directional light intensity (0-1) */
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
    };
} bf_cmd;

/* --- Picking --- */

typedef enum {
    BF_PICK_SKY,       /* ray hit nothing */
    BF_PICK_GROUND,    /* ray hit the ground plane */
    BF_PICK_ENTITY     /* ray hit an entity sprite */
} bf_pick_type;

typedef struct {
    bf_pick_type type;
    int entity_id;       /* entity ID when type == BF_PICK_ENTITY; 0 otherwise */
    vector position;     /* world-space hit point for GROUND and ENTITY; zeroed for SKY */
} bf_pick_result;

/* --- Engine --- */

typedef struct bf_engine bf_engine;

bf_engine  *bf_create(bf_config config);
void        bf_destroy(bf_engine *e);

int         bf_register_sprite(bf_engine *e, bf_sprite_def def);
void        bf_set_map(bf_engine *e, bf_map map);

int         bf_command(bf_engine *e, bf_cmd cmd);
void        bf_tick(bf_engine *e, float dt);
void        bf_render(bf_engine *e, uint32_t *pixel_buf);
bf_pick_result bf_pick(bf_engine *e, int screen_x, int screen_y);

#endif /* BATTLEFORGE_H */
