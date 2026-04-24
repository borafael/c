#ifndef RT_MESH_H
#define RT_MESH_H

#include "scene.h"

/* Ray / triangle-mesh intersection.
 *
 * The mesh is an index buffer over scene_vertex (position, normal, uv).
 * Hit reporting gives the ray parameter, the interpolated surface normal
 * (barycentric blend of vertex normals), and an interpolated uv so the
 * existing material/texture path works unchanged.
 *
 * This POC does a linear triangle scan. For meshes above a few hundred
 * triangles, swap in a BVH at this seam without touching callers.
 *
 * The OpenGL backend expands mesh triangles into the same flat triangle
 * SSBO as scene_triangle at upload time, so meshes render on the GPU with
 * face normals (flat shading). Per-vertex normal interpolation is CPU-only.
 */
typedef struct {
    float  t;
    vector normal;
    float  u, v;
} rt_mesh_hit;

int rt_intersect_mesh(vector ro, vector rd, const scene_mesh *mesh,
                      rt_mesh_hit *out);

/* Build or rebuild the CPU BVH for `mesh`. Call after any vertex-position
 * mutation (load, transform bake). Frees any prior accel cache. May
 * reorder mesh->indices. Safe to call on an empty mesh. */
void rt_mesh_build_bvh(scene_mesh *mesh);

/* Convenience: rt_mesh_build_bvh on every mesh in the scene. */
void rt_scene_build_accel(scene *s);

#endif /* RT_MESH_H */
