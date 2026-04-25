#ifndef RT_SCENE_ACCEL_H
#define RT_SCENE_ACCEL_H

#include "matrix.h"
#include "scene.h"

/* Per-frame scratch shared across renderer backends. Holds the resolved
 * node-world matrices plus per-mesh world / world_inv. Owns its
 * allocations and grows them on demand; reuse one instance per renderer
 * across frames. */
typedef struct {
    mat4 *node_world;             /* sized scene->node_count, NULL if 0 nodes */
    int   node_world_capacity;
    mat4 *mesh_world;             /* sized scene->mesh_count, identity if no node owns */
    mat4 *mesh_world_inv;         /* sized scene->mesh_count */
    int   mesh_capacity;
} rt_scene_accel;

void rt_scene_accel_init(rt_scene_accel *a);
void rt_scene_accel_dispose(rt_scene_accel *a);

/* Resolve node world transforms, run skinning (mutates skinned mesh
 * vertices and rebuilds their BVH), and fill mesh_world / mesh_world_inv
 * for every mesh. Returns 1 on success, 0 if alloc failed (in which
 * case the mesh_world / mesh_world_inv arrays may be stale; renderers
 * should fall back to identity). The scene must outlive the call.
 *
 * Pass a non-const scene because skinning rewrites mesh vertex data. */
int rt_scene_accel_resolve(rt_scene_accel *a, scene *s);

#endif
