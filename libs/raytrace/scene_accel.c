#include "scene_accel.h"
#include "cpu/bvh.h"
#include <stdlib.h>

void rt_scene_accel_init(rt_scene_accel *a) {
    a->node_world = NULL;
    a->node_world_capacity = 0;
    a->mesh_world = NULL;
    a->mesh_world_inv = NULL;
    a->mesh_capacity = 0;
}

void rt_scene_accel_dispose(rt_scene_accel *a) {
    free(a->node_world);
    free(a->mesh_world);
    free(a->mesh_world_inv);
    a->node_world = NULL;
    a->mesh_world = NULL;
    a->mesh_world_inv = NULL;
    a->node_world_capacity = 0;
    a->mesh_capacity = 0;
}

int rt_scene_accel_resolve(rt_scene_accel *a, scene *s) {
    if (s->mesh_count <= 0) return 1;

    if (a->mesh_capacity < s->mesh_count) {
        mat4 *gw = realloc(a->mesh_world,
                           sizeof(mat4) * (size_t)s->mesh_count);
        if (!gw) return 0;
        a->mesh_world = gw;
        mat4 *gwi = realloc(a->mesh_world_inv,
                            sizeof(mat4) * (size_t)s->mesh_count);
        if (!gwi) return 0;
        a->mesh_world_inv = gwi;
        a->mesh_capacity = s->mesh_count;
    }
    mat4 ident = mat4_identity();
    for (int i = 0; i < s->mesh_count; i++) {
        a->mesh_world[i] = ident;
        a->mesh_world_inv[i] = ident;
    }

    if (s->node_count <= 0) return 1;

    if (a->node_world_capacity < s->node_count) {
        mat4 *grown = realloc(a->node_world,
                              sizeof(mat4) * (size_t)s->node_count);
        if (!grown) return 1;  /* identity is still correct */
        a->node_world = grown;
        a->node_world_capacity = s->node_count;
    }
    scene_resolve_world_transforms(s, a->node_world);

    /* Skinning pass: deform skinned meshes from rest pose into mesh-local
     * space, then rebuild each skinned mesh's BVH (vertices moved). Rigid
     * meshes are untouched. */
    if (s->skin_count > 0) {
        scene_apply_skinning(s, a->node_world);
        for (int mi = 0; mi < s->mesh_count; mi++) {
            if (s->meshes[mi].skin_index >= 0) {
                rt_bvh_build(&s->meshes[mi]);
            }
        }
    }

    /* If multiple nodes reference the same mesh, the LAST one wins.
     * In practice each FBX-emitted mesh is owned by exactly one node. */
    for (int i = 0; i < s->node_count; i++) {
        int mi = s->nodes[i].mesh_index;
        if (mi >= 0 && mi < s->mesh_count) {
            a->mesh_world[mi]     = a->node_world[i];
            a->mesh_world_inv[mi] = mat4_affine_inverse(a->node_world[i]);
        }
    }
    return 1;
}
