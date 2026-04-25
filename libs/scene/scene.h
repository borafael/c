#ifndef SCENE_H
#define SCENE_H

#include <stdint.h>
#include "vector.h"

/* =========================================================================
 * libs/scene — renderer-agnostic scene representation.
 *
 * The scene is a graphical description of a frame to be rendered. Game
 * state (entities, physics bodies, AI, ...) is extracted into a scene
 * each frame; renderers (raytracer, future wireframe/rasterizer) consume
 * the scene read-only and produce pixels.
 *
 * Design invariants:
 *   - Scene types are pure data. No intersection logic, no acceleration
 *     structures. Those live in renderers.
 *   - Field semantics are conceptually universal. Each renderer reads
 *     what it cares about and ignores the rest (a wireframe renderer
 *     ignores texture_index and reflectivity; a flat-shader ignores
 *     procedural textures).
 *   - The scene never references game state: no entity IDs, no physics
 *     bodies, no gameplay flags.
 *   - The camera is a sibling type but NOT a field of `scene` — one
 *     world, potentially many views. Renderers take scene and camera
 *     separately.
 *
 * Ownership:
 *   - Texture pixels, sprite frame pixels, heightfield buffers are
 *     BORROWED — the caller retains ownership and must keep them alive
 *     while the scene references them. scene_destroy frees only the
 *     top-level arrays it grew.
 *   - Mesh vertex and index buffers passed to scene_add_mesh ARE owned
 *     by the scene and freed by scene_destroy.
 * ========================================================================= */

/* ============================== Color ==================================== */
typedef struct {
    uint8_t r, g, b;
} scene_color;

/* ============================== Texture ================================== */
typedef struct {
    uint32_t *pixels;   /* ARGB8888, NOT owned by the scene */
    int width;
    int height;
} scene_texture;

typedef enum {
    SCENE_TEX_NONE     = 0,
    SCENE_TEX_CHECKER  = 1,
    SCENE_TEX_IMAGE    = 2,
    SCENE_TEX_GRADIENT = 3,   /* albedo → albedo2 along +Y over tex_scale units */
    SCENE_TEX_NOISE    = 4,   /* value-noise lerp, tex_scale = world/cell */
    SCENE_TEX_WOOD     = 5,   /* turbulent rings in XZ, tex_scale = ring width */
    SCENE_TEX_MARBLE   = 6,   /* turbulent veins along X, tex_scale = band width */
    SCENE_TEX_CELLS    = 7,   /* Voronoi F1: smooth cell interiors */
    SCENE_TEX_CRACKS   = 8,   /* Voronoi F2-F1: cracks at cell borders */
    SCENE_TEX_STRIPES  = 9,   /* alternating bands along X */
    SCENE_TEX_DOTS     = 10,  /* polka dots in XZ grid */
    SCENE_TEX_BRICKS   = 11,  /* 2:1 staggered bricks in XZ */
    SCENE_TEX_CLOUDS   = 12,  /* soft fBm clouds */
    SCENE_TEX_SPOTS    = 13,  /* thresholded noise (leopard) */
} scene_tex_kind;

/* ============================== Material ================================= */
typedef struct {
    scene_color    albedo;        /* base color; checker: tile A */
    scene_color    albedo2;       /* checker: tile B */
    scene_tex_kind tex_kind;
    float          tex_scale;     /* checker: world units per tile; image: UV repeat */
    int            tex_index;     /* SCENE_TEX_IMAGE: index into scene->textures */
    float          reflectivity;  /* 0 = matte, 1 = perfect mirror */
    int            unlit;         /* 1 = skip shading, use raw albedo */
} scene_material;

scene_material scene_material_default(void);

/* ============================== Primitives =============================== */
typedef struct {
    vector center;
    float  radius;
    int    material;
} scene_sphere;

typedef struct {
    vector normal;
    vector point;
    int    material;
} scene_plane;

typedef struct {
    vector center;
    vector normal;
    float  radius;
    int    material;
} scene_disc;

typedef struct {
    vector center;
    vector axis;
    float  radius;
    float  half_height;
    int    material;
} scene_cylinder;

typedef struct {
    vector v0, v1, v2;
    int    material;
} scene_triangle;

typedef struct {
    vector min;
    vector max;
    int    material;
} scene_box;

typedef struct {
    uint32_t *pixels;   /* ARGB8888, NOT owned */
    int       width;
    int       height;
} scene_frame;

typedef struct {
    vector       position;      /* center in world space */
    vector       direction;     /* facing direction (for angle selection only) */
    float        width;         /* world-space quad width */
    float        height;        /* world-space quad height */
    int          frame_count;   /* number of viewing angles */
    scene_frame *frames;        /* NOT owned */
} scene_sprite;

typedef struct {
    float   *heights;           /* rows*cols vertex heights (NOT owned) */
    uint8_t *colors;            /* (rows-1)*(cols-1)*3 RGB per cell (NOT owned) */
    float   *normals;           /* rows*cols*3 vertex normals (NOT owned) */
    int      rows, cols;
    float    world_width;
    float    world_depth;
    float    origin_x, origin_z;
    float    max_height;
    int      material;          /* scene material index, or -1 for raw cell colors */
} scene_heightfield;

/* ============================== Meshes ===================================
 *
 * Forward-looking. Not yet consumed by the raytracer; included so game
 * code and future renderers can start building against the type.
 * Vertex and index buffers ARE owned by the scene.
 */
typedef struct {
    vector position;
    vector normal;
    float  u, v;
} scene_vertex;

typedef struct {
    scene_vertex *vertices;
    int           vertex_count;
    uint32_t     *indices;        /* 3 per triangle */
    int           index_count;
    int           material_index;
    /* Bounding sphere for O(1) ray-mesh reject. Populated by the OBJ
     * loader; call scene_mesh_compute_bounds after any post-load vertex
     * transformation to keep them in sync. A radius of 0 means "no
     * bounds known" and renderers must iterate all triangles. */
    vector        bounds_center;
    float         bounds_radius;
    /* Opaque renderer-built acceleration cache (BVH on CPU). Owned by
     * the scene — freed by scene_destroy / scene_clear. Renderers are
     * responsible for building + interpreting the bytes; scene code just
     * allocates and frees. accel == NULL means "no accel, fall back to
     * linear triangle scan." */
    void         *accel;
    int           accel_count;    /* renderer-defined units (e.g. BVH nodes) */
} scene_mesh;

/* Recompute bounds_center / bounds_radius from the current vertex positions.
 * Safe to call on an empty mesh (sets radius = 0). */
void scene_mesh_compute_bounds(scene_mesh *mesh);

/* ============================== Lights =================================== */
typedef struct {
    vector direction;
    float  intensity;
} scene_light;

/* ============================== Nodes ====================================
 *
 * A node is a transform in space, optionally carrying a mesh. Nodes form
 * a tree via parent_index — a node's world transform is the product of
 * its local transform and all ancestor transforms. This is the hook for
 * skeletal-style rigid animation: upper arm is parented to shoulder,
 * forearm to upper arm, etc. The animator rewrites local transforms each
 * frame; renderers resolve world transforms when intersecting meshes.
 *
 * A mesh_index of -1 denotes a transform-only "joint" node.
 * A parent_index of -1 denotes a root (no parent). Parents must appear
 * before their children in scene->nodes so a single forward pass can
 * resolve world transforms.
 */
typedef struct {
    vector position;
    vector rotation;   /* Euler XYZ, radians */
    vector scale;      /* (1,1,1) for no scaling */
} scene_transform;

scene_transform scene_transform_identity(void);

typedef struct {
    char            name[64];     /* loader-provided identifier; "" if unset */
    scene_transform transform;    /* local, relative to parent */
    int             mesh_index;   /* into scene->meshes, or -1 */
    int             parent_index; /* into scene->nodes, or -1 for root */
} scene_node;

/* ============================== Animation ================================
 *
 * An animation is a bundle of per-node transform tracks sampled over time.
 * Format-agnostic: populated today by the FBX loader, potentially glTF or
 * hand-authored keyframes later. Purely data — sampling lives in
 * scene_anim_sample, which writes into scene_node.transform. Renderers see
 * only the resolved node transforms and remain animation-unaware.
 *
 * Each track targets one node and one of its 9 transform channels
 * (pos.x/y/z, rot.x/y/z, scale.x/y/z). Keys are (time_seconds, value) with
 * linear interpolation between them — sufficient for rigid animation;
 * cubic curves can be added later without breaking callers.
 *
 * Ownership: keys arrays and tracks arrays are owned by the scene and
 * freed by scene_destroy / scene_clear.
 */
typedef enum {
    SCENE_ANIM_POS_X = 0, SCENE_ANIM_POS_Y, SCENE_ANIM_POS_Z,
    SCENE_ANIM_ROT_X,     SCENE_ANIM_ROT_Y, SCENE_ANIM_ROT_Z,
    SCENE_ANIM_SCL_X,     SCENE_ANIM_SCL_Y, SCENE_ANIM_SCL_Z,
    SCENE_ANIM_CHANNEL_COUNT
} scene_anim_channel;

typedef struct {
    float time;     /* seconds from clip start */
    float value;
} scene_anim_key;

typedef struct {
    int                 node_index;    /* into scene->nodes */
    scene_anim_channel  channel;
    scene_anim_key     *keys;          /* owned by the scene */
    int                 key_count;
} scene_anim_track;

typedef struct {
    char              name[64];        /* e.g. "Walk", "Idle" */
    float             duration;        /* seconds; 0 = still pose */
    scene_anim_track *tracks;          /* owned by the scene */
    int               track_count;
} scene_animation;

/* scene_anim_sample is declared below, after the `scene` type. */

/* ============================== Camera ===================================
 *
 * Camera lives in the scene lib (it's a graphics concept) but is NOT a
 * field of `scene` — one world can be viewed by many cameras. Renderers
 * take a scene* and a camera* as separate parameters.
 *
 * scene_camera_place / scene_camera_create recompute right/up from
 * forward + world-up. Callers that set forward directly should call
 * scene_camera_refresh to re-derive the orthonormal basis.
 */
typedef struct {
    vector origin;
    vector forward;
    vector right;
    vector up;
} scene_camera;

scene_camera *scene_camera_create(vector position, vector direction);
void          scene_camera_place(scene_camera *cam, vector position, vector direction);
void          scene_camera_destroy(scene_camera *cam);
void          scene_camera_get_basis(const scene_camera *cam,
                                     vector *origin, vector *forward,
                                     vector *right, vector *up);

/* ============================== Scene ==================================== */
typedef struct {
    scene_sphere      *spheres;      int sphere_count,      sphere_capacity;
    scene_plane       *planes;       int plane_count,       plane_capacity;
    scene_disc        *discs;        int disc_count,        disc_capacity;
    scene_cylinder    *cylinders;    int cylinder_count,    cylinder_capacity;
    scene_triangle    *triangles;    int triangle_count,    triangle_capacity;
    scene_box         *boxes;        int box_count,         box_capacity;
    scene_sprite      *sprites;      int sprite_count,      sprite_capacity;
    scene_heightfield *heightfields; int heightfield_count, heightfield_capacity;
    scene_light       *lights;       int light_count,       light_capacity;
    scene_material    *materials;    int material_count,    material_capacity;
    scene_texture     *textures;     int texture_count,     texture_capacity;
    scene_mesh        *meshes;       int mesh_count,        mesh_capacity;
    scene_node        *nodes;        int node_count,        node_capacity;
    scene_animation   *animations;   int animation_count,   animation_capacity;
    float              ambient;
} scene;

/* Lifecycle */
scene *scene_create(void);
void   scene_destroy(scene *s);
void   scene_clear(scene *s);

/* Mutation.
 *
 * Every scene_add_* returns the index of the added item on success, or
 * -1 on allocation failure. Callers that don't need the index may ignore
 * the return value.
 *
 * scene_add_mesh takes ownership of mesh.vertices and mesh.indices
 * (freed by scene_destroy). All other add functions copy by value; the
 * caller retains ownership of pointer fields (texture pixels, sprite
 * frames, heightfield buffers).
 */
int scene_add_sphere(scene *s, scene_sphere sphere);
int scene_add_plane(scene *s, scene_plane plane);
int scene_add_disc(scene *s, scene_disc disc);
int scene_add_cylinder(scene *s, scene_cylinder cylinder);
int scene_add_triangle(scene *s, scene_triangle triangle);
int scene_add_box(scene *s, scene_box box);
int scene_add_sprite(scene *s, scene_sprite sprite);
int scene_add_heightfield(scene *s, const scene_heightfield *hf);
int scene_add_light(scene *s, scene_light light);
int scene_add_material(scene *s, scene_material material);
int scene_add_texture(scene *s, scene_texture texture);
int scene_add_mesh(scene *s, scene_mesh mesh);
int scene_add_node(scene *s, scene_node node);

/* Returns the index of the first node whose name matches `name`, or -1
 * if none. Case-sensitive, exact match. Use after importing a rig to
 * resolve incoming animation tracks from a separate FBX. */
int scene_find_node_by_name(const scene *s, const char *name);

/* Appends an animation. Takes ownership of `anim.tracks` (and each
 * track's keys array) — freed by scene_destroy / scene_clear. */
int scene_add_animation(scene *s, scene_animation anim);

/* Sample `anim` at time `t` (seconds), writing into the local transforms
 * of the nodes each track targets. If loop != 0, wraps via fmodf(t,
 * duration); otherwise clamps to [0, duration]. Nodes and channels not
 * covered by a track are left untouched — callers should seed node
 * transforms to a neutral pose before sampling if desired. */
void scene_anim_sample(scene *s, const scene_animation *anim,
                       float t, int loop);

void scene_set_ambient(scene *s, float ambient);

#endif /* SCENE_H */
