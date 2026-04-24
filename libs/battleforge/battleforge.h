#ifndef BATTLEFORGE_H
#define BATTLEFORGE_H

#include <stdint.h>
#include "vector.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include "plane.h"
#include "box.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "sprite.h"
#include "heightfield.h"
#include "renderer.h"   /* rt_backend */

/* Forward declaration — full definition in slice.h */
typedef struct slice_sheet slice_sheet;

/* --- Visual descriptor ---
 * Tagged union describing how an entity is drawn. New primitive kinds
 * (box, cylinder, disc, ...) slot in by extending the enum and union. */
typedef enum {
    BF_VIS_NONE   = 0,
    BF_VIS_SPRITE = 1,
    BF_VIS_SPHERE = 2,
} bf_visual_kind;

typedef struct {
    bf_visual_kind kind;
    union {
        struct { int sheet_id; } sprite;
        struct { float radius; scene_material material; } sphere;
    };
} bf_visual_desc;

/* --- Configuration --- */

typedef struct {
    int render_width;
    int render_height;
    float fov;
    int num_threads;
    /* Preferred raytrace backend. Zero-initialized callers get
       RT_BACKEND_CPU (=0), matching the historical default. If the
       requested backend isn't compiled in, bf_create falls back to
       CPU. */
    rt_backend backend;
} bf_config;

/* --- Map --- */

typedef struct {
    float width;
    float depth;
    int grid_cols, grid_rows;   /* vertex dimensions (e.g., 64x64) */
    float *heights;             /* grid_rows * grid_cols floats */
    uint8_t *colors;            /* (grid_rows-1) * (grid_cols-1) * 3 (RGB per cell) */
    float *normals;             /* grid_rows * grid_cols * 3 (vertex normals) */
    float max_height;           /* maximum terrain height */
    float ambient;
    vector light_dir;
    float light_intensity;
    scene_material terrain_material;  /* modulates per-cell colors at render time.
                                      tex_kind=NONE (zero-initialized) keeps
                                      the legacy raw-color / zero-reflectivity
                                      behavior. */
    scene_material sky_material;      /* material for the optional sky sphere —
                                      GRADIENT is the natural choice (horizon→
                                      zenith along +Y). */
    float sky_radius;              /* 0 = no sky sphere; positive = giant
                                      camera-centered sphere of this radius
                                      gets added to the scene each frame. */
} bf_map;

/* --- ECS Components --- */

typedef enum {
    BF_COMP_NONE       = 0,
    BF_COMP_POSITION   = (1 << 0),
    BF_COMP_VISUAL     = (1 << 1),
    BF_COMP_LOCOMOTION = (1 << 2),
    BF_COMP_SELECTION  = (1 << 3),
} bf_component;

typedef struct { vector position; vector direction; } bf_position;
typedef struct {
    bf_visual_desc desc;
    /* Per-instance animation state — only read when desc.kind == BF_VIS_SPRITE. */
    int anim_index;
    int anim_frame;
    float frame_timer;
    float anim_fps;
} bf_visual;
typedef enum { BF_LOCO_LINEAR, BF_LOCO_PARABOLIC, BF_LOCO_INSTANT } bf_loco_type;
typedef struct { vector origin; vector target; float speed; float progress; } bf_trajectory_linear;
typedef struct { vector origin; vector target; float speed; float progress; float arc_height; } bf_trajectory_parabolic;
typedef struct { bf_loco_type type; union { bf_trajectory_linear linear; bf_trajectory_parabolic parabolic; }; } bf_locomotion;
typedef struct { int selected; } bf_selection;

/* --- Unit Definitions --- */

#define BF_UNIT_NAME_SIZE 32
#define MAX_UNIT_DEFS 256
typedef struct {
    char name[BF_UNIT_NAME_SIZE];
    bf_visual_desc visual;
    float base_speed;
    int has_selection;
} bf_unit_def;

/* --- Map Registry --- */

#define BF_MAP_NAME_SIZE 32
#define MAX_MAPS 64

/* --- Commands --- */

typedef enum {
    BF_CMD_CAMERA_SET,
    BF_CMD_CAMERA_MOVE,
    BF_CMD_ENTITY_CREATE,
    BF_CMD_ENTITY_DESTROY,
    BF_CMD_ENTITY_MOVE,
    BF_CMD_ENTITY_FACE,
    BF_CMD_REGISTER_UNIT,
    BF_CMD_LOAD_MAP,
    BF_CMD_SELECT_MAP,
    BF_CMD_SELECT,
    BF_CMD_ENTITY_ANIMATE,
    BF_CMD_COUNT
} bf_cmd_type;

typedef struct {
    bf_cmd_type type;
    union {
        struct { vector position; vector direction; } camera_set;
        struct { vector delta; } camera_move;
        struct { int id; int unit_def_id; vector position; vector direction; } entity_create;
        struct { int id; } entity_destroy;
        struct { int id; vector target; float speed; bf_loco_type loco_type; } entity_move;
        struct { int id; vector direction; } entity_face;
        struct { bf_unit_def def; } register_unit;
        struct { char name[BF_MAP_NAME_SIZE]; bf_map *map; } load_map;  /* engine takes ownership */
        struct { int index; } select_map;
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

/* --- Logging --- */

typedef enum {
    BF_LOG_INFO,
    BF_LOG_WARN,
    BF_LOG_ERROR
} bf_log_level;

#define BF_LOG_TEXT_SIZE 256
#define BF_LOG_BUFFER_SIZE 512

typedef struct {
    bf_log_level level;
    char text[BF_LOG_TEXT_SIZE];
} bf_log_entry;

/* --- Engine --- */

typedef struct bf_engine bf_engine;

bf_engine  *bf_create(bf_config config);
void        bf_destroy(bf_engine *e);

/* Swap the active raytrace backend. Returns 0 on success, -1 if the
   requested backend isn't available or allocation fails (the engine
   keeps its previous renderer in that case). */
int         bf_set_backend(bf_engine *e, rt_backend backend);
rt_backend  bf_get_backend(const bf_engine *e);

int         bf_register_sprite(bf_engine *e, slice_sheet *sheet,
                               float world_width, float world_height);
void        bf_set_map(bf_engine *e, bf_map map);
void        bf_map_generate_test_terrain(bf_map *map);
/* Fills map->colors using heights+normals via a slope-aware palette
   (marsh/grass/tundra blended by height, with rock overlaid on cliffs
   and subtle per-cell noise). Requires heights, normals, grid_rows,
   grid_cols, and colors to already be allocated/computed. */
void        bf_map_colorize(bf_map *map);
float       bf_map_height_at(const bf_map *map, float x, float z);

int         bf_command(bf_engine *e, bf_cmd cmd);
void        bf_tick(bf_engine *e, float dt);
void        bf_render(bf_engine *e, uint32_t *pixel_buf);
bf_pick_result bf_pick(bf_engine *e, int screen_x, int screen_y);

/* --- Logging --- */
void            bf_log(bf_engine *e, bf_log_level level, const char *fmt, ...);
int             bf_log_count(const bf_engine *e);
const bf_log_entry *bf_log_get(const bf_engine *e, int index);

#endif /* BATTLEFORGE_H */
