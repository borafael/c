#ifndef SCENE_FBX_H
#define SCENE_FBX_H

#include "scene.h"

/* FBX loader. Imports meshes, the node hierarchy, materials, and animation
 * clips from a binary or ASCII .fbx file via vendored ufbx.
 *
 * Mirrors scene/obj.h: a raw loader that emits owned buffers, plus a
 * convenience that appends directly to a scene. Unlike OBJ, FBX carries a
 * node tree and named animation clips, so the scene-level entry point
 * writes into scene->nodes and scene->animations in addition to
 * ->meshes / ->materials.
 *
 * Skinning: meshes with a skin deformer are imported as scene_skin records
 * alongside the mesh. The runtime then deforms those meshes per frame via
 * scene_apply_skinning. Meshes without skin deformers follow the rigid
 * path (one mesh per node, transform applied at intersect time). Both
 * styles coexist in a single scene.
 *
 * Euler convention: ufbx stores rotation as a quaternion; we convert to
 * Euler XYZ at load time, with ufbx's degrees rescaled to radians (the
 * project-wide unit). Near gimbal lock (pitch ±90°) baked animation
 * tracks may exhibit angle flipping — not a concern for the typical
 * rigid-limb case.
 */

typedef enum {
    SCENE_FBX_DEFAULT        = 0,
    SCENE_FBX_SKIP_ANIMATION = 1 << 1,  /* drop all anim clips */
    /* Load only the anim_stacks from the FBX, not the node tree / meshes
     * / materials. Each baked track's node_index is resolved by looking
     * up ufbx_node->name in the destination scene's existing nodes;
     * tracks whose nodes don't match are dropped with a warning. Use
     * this to layer walk.fbx / run.fbx / jump.fbx clips onto a rig
     * imported separately. Only valid with scene_add_fbx (needs the
     * destination scene to resolve names); scene_load_fbx rejects it. */
    SCENE_FBX_ANIMATION_ONLY = 1 << 2,
} scene_fbx_flags;

/* Animation bake rate in Hz. FBX curves are baked to linear keyframes at
 * this rate when emitting scene_animation. 30 Hz is standard for games. */
#define SCENE_FBX_BAKE_HZ 30

/* Raw loader output. The caller owns the arrays and their nested buffers
 * (vertices/indices in each mesh, tracks+keys in each animation, bones/
 * influences/rest pose in each skin) and must call scene_fbx_result_free.
 *
 * Skin indices in `meshes[i].skin_index` are RELATIVE to this result's
 * `skins[]` array. scene_add_fbx rebases them when appending to a scene. */
typedef struct {
    scene_mesh       *meshes;      int mesh_count;
    scene_skin       *skins;       int skin_count;
    scene_node       *nodes;       int node_count;
    scene_material   *materials;   int material_count;
    scene_animation  *animations;  int animation_count;
} scene_fbx_result;

/* Returns 1 on success (out populated), 0 on failure (out zero-initialized). */
int  scene_load_fbx(const char *path, scene_fbx_flags flags,
                    scene_fbx_result *out);

/* Frees all buffers owned by a result. Safe to call on a zeroed result. */
void scene_fbx_result_free(scene_fbx_result *r);

/* Loads an FBX and appends all emitted arrays to `s`, rewriting
 * material/mesh/node indices to the scene's post-append offsets. If
 * first_node_index_out is non-NULL, it receives scene->nodes index of the
 * first added node. Returns the number of nodes added, or -1 on failure. */
int  scene_add_fbx(scene *s, const char *path, scene_fbx_flags flags,
                   int *first_node_index_out);

#endif /* SCENE_FBX_H */
