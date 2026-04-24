#ifndef SCENE_OBJ_H
#define SCENE_OBJ_H

#include "scene.h"

/* Wavefront OBJ + MTL loaders.
 *
 * scene_load_obj / scene_add_mesh_from_obj: single-material loaders that
 * produce one scene_mesh covering the whole file. `usemtl` directives and
 * MTL files are ignored — callers that want authentic multi-colored models
 * should use scene_add_meshes_from_obj instead.
 *
 * scene_load_mtl: reads a Wavefront MTL file into an array of named
 * material entries keyed by `newmtl` name. Only `Kd` (diffuse) is used —
 * it populates scene_material.albedo. Other fields fall back to
 * scene_material_default.
 *
 * scene_add_meshes_from_obj: splits an OBJ by `usemtl` group, emitting one
 * scene_mesh per group into the scene. Material names are resolved against
 * the mtl_entries argument; unknown or absent names fall back to
 * default_material_index. Materials referenced by the OBJ are added to
 * scene->materials on demand (once each).
 *
 * Face parsing in both paths supports "v", "v/vt", "v//vn", "v/vt/vn";
 * triangulates n-gons by fan; honors 1-based and negative OBJ indices;
 * generates face normals when vn directives are absent.
 */

/* Returns 1 on success (out populated with owned buffers), 0 on failure
 * (out zero-initialized). On success the caller is responsible for the
 * vertex / index buffers, typically by passing `*out` to scene_add_mesh. */
int scene_load_obj(const char *path, int material_index, scene_mesh *out);

/* Convenience: load and add to scene in one call.
 * Returns the mesh index, or -1 on failure. */
int scene_add_mesh_from_obj(scene *s, const char *path, int material_index);

/* One parsed entry from an MTL file. */
typedef struct {
    char           name[64];
    scene_material material;
} scene_mtl_entry;

/* Parses `path` as Wavefront MTL. On success returns the number of entries
 * parsed (>= 0) and allocates *out_entries (caller frees with free()).
 * Returns -1 on failure. */
int scene_load_mtl(const char *path, scene_mtl_entry **out_entries);

/* Loads an OBJ, splitting by usemtl group, and appends one scene_mesh per
 * group to `scene`. Material names are resolved against mtl_entries; groups
 * with no usemtl or unknown names fall back to default_material_index. MTL
 * materials referenced by the OBJ get added to scene->materials on first
 * use.
 *
 * Returns the number of meshes added (>= 0) on success, -1 on failure. If
 * first_mesh_index_out is non-NULL, it receives scene->meshes index of the
 * first added mesh. */
int scene_add_meshes_from_obj(scene *s,
                              const char *obj_path,
                              const scene_mtl_entry *mtl_entries,
                              int mtl_count,
                              int default_material_index,
                              int *first_mesh_index_out);

#endif /* SCENE_OBJ_H */
