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
} scene_mesh;

/* ============================== Lights =================================== */
typedef struct {
    vector direction;
    float  intensity;
} scene_light;

/* ============================== Nodes ====================================
 *
 * Forward-looking — transform + optional mesh reference. Flat list (no
 * hierarchy) for v0. A mesh_index of -1 denotes a transform-only node.
 */
typedef struct {
    vector position;
    vector rotation;   /* Euler XYZ, radians */
    vector scale;      /* (1,1,1) for no scaling */
} scene_transform;

scene_transform scene_transform_identity(void);

typedef struct {
    scene_transform transform;
    int             mesh_index;   /* into scene->meshes, or -1 */
} scene_node;

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

void scene_set_ambient(scene *s, float ambient);

#endif /* SCENE_H */
