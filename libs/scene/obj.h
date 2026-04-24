#ifndef SCENE_OBJ_H
#define SCENE_OBJ_H

#include "scene.h"

/* Wavefront OBJ loader.
 *
 * Parses v / vn / vt / f lines and produces a scene_mesh with heap-allocated
 * vertices and indices ready to be handed to scene_add_mesh (which takes
 * ownership). Supports the four face vertex forms: "v", "v/vt", "v//vn",
 * "v/vt/vn". N-gon faces are triangulated by fan. OBJ 1-based and negative
 * indices are both honored.
 *
 * If the file has no vn directives, per-triangle face normals are generated.
 * If it has no vt, uvs are left at zero.
 *
 * Material groups (g / usemtl) are ignored for now — the whole mesh gets the
 * single material_index passed in. MTL files are not parsed.
 */

/* Returns 1 on success (out populated with owned buffers), 0 on failure
 * (out zero-initialized). On success the caller is responsible for the
 * vertex / index buffers, typically by passing `*out` to scene_add_mesh. */
int scene_load_obj(const char *path, int material_index, scene_mesh *out);

/* Convenience: load and add to scene in one call.
 * Returns the mesh index, or -1 on failure. */
int scene_add_mesh_from_obj(scene *s, const char *path, int material_index);

#endif /* SCENE_OBJ_H */
