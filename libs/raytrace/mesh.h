#ifndef RT_MESH_H
#define RT_MESH_H

#include "scene.h"
#include "matrix.h"

/* Ray / triangle-mesh intersection.
 *
 * The mesh is an index buffer over scene_vertex (position, normal, uv).
 * Hit reporting gives the ray parameter, the interpolated surface normal
 * (barycentric blend of vertex normals), and an interpolated uv so the
 * existing material/texture path works unchanged.
 *
 * Optional `world_inv` argument: the inverse of the mesh's world-space
 * transform (typically resolved from scene_node via
 * scene_resolve_world_transforms). When non-NULL, the ray is transformed
 * into mesh-local space at entry and the output normal is rotated back
 * to world space — BVH AABBs and triangle vertices stay in their stored
 * local space. NULL means "treat the mesh as already in world space"
 * (back-compat for OBJ-loader callers that don't use nodes).
 *
 * The returned `t` is the world-space ray parameter regardless: an
 * affine transform preserves ray-parameter t since
 * local_ro + t*local_rd = M_inv * (world_ro + t*world_rd).
 *
 * The OpenGL backend expands mesh triangles into the same flat triangle
 * SSBO as scene_triangle at upload time, so meshes render on the GPU with
 * face normals (flat shading). Per-vertex normal interpolation is CPU-only.
 * The GPU backend does NOT yet honor scene_node transforms — meshes there
 * still render in their stored vertex space.
 */
typedef struct {
    float  t;
    vector normal;
    float  u, v;
} rt_mesh_hit;

int rt_intersect_mesh(vector ro, vector rd, const scene_mesh *mesh,
                      const mat4 *world_inv, rt_mesh_hit *out);

/* Build or rebuild the CPU BVH for `mesh`. Call after any vertex-position
 * mutation (load, transform bake). Frees any prior accel cache. May
 * reorder mesh->indices. Safe to call on an empty mesh. */
void rt_mesh_build_bvh(scene_mesh *mesh);

/* Convenience: rt_mesh_build_bvh on every mesh in the scene. */
void rt_scene_build_accel(scene *s);

#endif /* RT_MESH_H */
