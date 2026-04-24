#ifndef RT_CPU_BVH_H
#define RT_CPU_BVH_H

#include "scene.h"

/* Binary BVH stored as a flat node array.
 *
 * Convention:
 *   - Nodes laid out in DFS order. An internal node's left child is
 *     always the very next node (`idx + 1`); the right child's offset
 *     from the parent is stored explicitly.
 *   - `tri_count > 0` marks a leaf: triangles [tri_start, tri_start+tri_count)
 *     refer to triangle indices in `mesh->indices` (stored as index
 *     triplets — triangle N uses indices[N*3..N*3+2]).
 *   - `tri_count == 0` marks an internal node.
 *
 * The builder reorders `mesh->indices` in place so leaves can address
 * contiguous triangle ranges. This is semantically transparent to other
 * code since a mesh's triangle set doesn't depend on order.
 */
typedef struct {
    float aabb_min[3];
    float aabb_max[3];
    int   tri_start;             /* leaf: first triangle index */
    int   tri_count;             /* leaf: triangles here; internal: 0 */
    int   second_child_offset;   /* internal: right-child node offset from self */
    int   _pad;                  /* pad to 32 bytes */
} rt_bvh_node;

/* Rebuilds the BVH for `mesh`. Frees any prior accel buffer. Safe to call
 * on an empty mesh (clears accel). May reorder mesh->indices. */
void rt_bvh_build(scene_mesh *mesh);

/* Slab-test ray against an AABB. Returns 1 on hit, with *t_enter set to
 * max(0, tmin). inv_rd should have each component 1/rd.axis. */
int  rt_bvh_ray_aabb(vector ro, vector inv_rd,
                     const float amin[3], const float amax[3],
                     float *t_enter);

#endif /* RT_CPU_BVH_H */
