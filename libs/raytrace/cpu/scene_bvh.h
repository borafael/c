#ifndef RT_CPU_SCENE_BVH_H
#define RT_CPU_SCENE_BVH_H

#include <stdint.h>
#include "scene.h"
#include "bvh.h"

/* Top-level BVH over a scene's bounded analytic primitives.
 *
 * Without it every ray tests every primitive, so render cost grows as
 * pixels x bounces x primitive_count. The CPU renderer rebuilds this
 * once per frame (scenes are rebuilt by apps every frame anyway; a build
 * over a few thousand primitives costs well under a millisecond) and
 * closest_hit() traverses it instead of looping.
 *
 * Covered: spheres (except portal straddlers, which spawn virtual copies
 * anywhere in space), discs, cylinders, cones, tori, triangles, boxes.
 * Not covered, still tested linearly: planes (unbounded), meshes (own
 * per-mesh BVH + portal virtuals), sprites (camera-facing), heightfields.
 *
 * Nodes reuse rt_bvh_node (DFS order, left child = idx + 1); for a leaf,
 * [tri_start, tri_start + tri_count) indexes `prims`, each entry an
 * RT_SBVH_REF(kind, index) with kind one of RT_OBJ_KIND_*.
 *
 * Bounds assume unit-length axes / normals / box bases, matching what the
 * intersectors assume. */

#define RT_SBVH_REF(kind, index) (((uint32_t)(kind) << 24) | ((uint32_t)(index) & 0x00FFFFFFu))
#define RT_SBVH_KIND(ref)        ((int)((ref) >> 24))
#define RT_SBVH_INDEX(ref)       ((int)((ref) & 0x00FFFFFFu))

typedef struct {
    rt_bvh_node *nodes;
    int          node_count, node_cap;
    uint32_t    *prims;            /* RT_SBVH_REF entries, leaf-ordered */
    int          prim_count, prim_cap;
    float       *bounds;           /* build scratch: 6 floats per prim */
    int          bounds_cap;
} rt_scene_bvh;

/* 1 if this sphere renders through the linear path (portal straddler). */
static inline int rt_sbvh_sphere_is_linear(const scene_sphere *s) {
    for (int p = 0; p < SCENE_MAX_PORTAL_TRAVERSALS; p++)
        if (s->portal_disc1[p] != 0 || s->portal_sphere1[p] != 0) return 1;
    return 0;
}

/* Rebuild for `s`, reusing buffers. On allocation failure the BVH is left
 * empty (prim_count == 0) and returns 0; callers must then fall back to
 * linear testing of every primitive. */
int  rt_scene_bvh_build(rt_scene_bvh *b, const scene *s);
void rt_scene_bvh_free(rt_scene_bvh *b);

#endif /* RT_CPU_SCENE_BVH_H */
