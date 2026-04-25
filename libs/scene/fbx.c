#include "fbx.h"
#include "ufbx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================ Helpers ================================ */

static vector vec3_from_ufbx(ufbx_vec3 v) {
    return (vector){ (float)v.x, (float)v.y, (float)v.z };
}

static scene_color color_from_ufbx(ufbx_vec3 c) {
    double r = c.x < 0 ? 0 : (c.x > 1 ? 1 : c.x);
    double g = c.y < 0 ? 0 : (c.y > 1 ? 1 : c.y);
    double b = c.z < 0 ? 0 : (c.z > 1 ? 1 : c.z);
    return (scene_color){ (uint8_t)(r * 255.0 + 0.5),
                          (uint8_t)(g * 255.0 + 0.5),
                          (uint8_t)(b * 255.0 + 0.5) };
}

/* ufbx_quat_to_euler returns degrees. scene_transform.rotation is
 * radians (consumed by mat4_rotate_xyz / sinf / cosf), so convert at
 * the loader boundary. */
#define UFBX_DEG_TO_RAD ((float)(M_PI / 180.0))

/* ufbx stores 4x3 column-major affine matrices; our mat4 is row-major
 * with an implied (0,0,0,1) bottom row. Plain repacking. */
static mat4 mat4_from_ufbx(ufbx_matrix u) {
    mat4 r;
    r.m[ 0] = (float)u.m00; r.m[ 1] = (float)u.m01; r.m[ 2] = (float)u.m02; r.m[ 3] = (float)u.m03;
    r.m[ 4] = (float)u.m10; r.m[ 5] = (float)u.m11; r.m[ 6] = (float)u.m12; r.m[ 7] = (float)u.m13;
    r.m[ 8] = (float)u.m20; r.m[ 9] = (float)u.m21; r.m[10] = (float)u.m22; r.m[11] = (float)u.m23;
    r.m[12] = 0.0f;         r.m[13] = 0.0f;         r.m[14] = 0.0f;         r.m[15] = 1.0f;
    return r;
}

static scene_transform transform_from_ufbx(ufbx_transform t) {
    ufbx_vec3 e = ufbx_quat_to_euler(t.rotation, UFBX_ROTATION_ORDER_XYZ);
    scene_transform out;
    out.position = vec3_from_ufbx(t.translation);
    out.rotation = (vector){ (float)e.x * UFBX_DEG_TO_RAD,
                             (float)e.y * UFBX_DEG_TO_RAD,
                             (float)e.z * UFBX_DEG_TO_RAD };
    out.scale    = vec3_from_ufbx(t.scale);
    return out;
}

static scene_material convert_material(const ufbx_material *m) {
    scene_material out = scene_material_default();
    /* Prefer FBX Phong diffuse_color; fall back to PBR base_color. */
    if (m->fbx.diffuse_color.has_value) {
        out.albedo = color_from_ufbx(m->fbx.diffuse_color.value_vec3);
    } else if (m->pbr.base_color.has_value) {
        out.albedo = color_from_ufbx(m->pbr.base_color.value_vec3);
    }
    if (m->fbx.reflection_factor.has_value) {
        float r = (float)m->fbx.reflection_factor.value_real;
        if (r < 0) r = 0;
        if (r > 1) r = 1;
        out.reflectivity = r;
    } else if (m->pbr.metalness.has_value) {
        float r = (float)m->pbr.metalness.value_real;
        if (r < 0) r = 0;
        if (r > 1) r = 1;
        out.reflectivity = r;
    }
    return out;
}

/* ================================ Result ================================= */

void scene_fbx_result_free(scene_fbx_result *r) {
    if (!r) return;
    for (int i = 0; i < r->mesh_count; i++) {
        free(r->meshes[i].vertices);
        free(r->meshes[i].indices);
        free(r->meshes[i].accel);
    }
    free(r->meshes);
    for (int i = 0; i < r->skin_count; i++) {
        free(r->skins[i].bones);
        free(r->skins[i].influences);
        free(r->skins[i].rest_positions);
        free(r->skins[i].rest_normals);
    }
    free(r->skins);
    free(r->nodes);
    free(r->materials);
    for (int i = 0; i < r->animation_count; i++) {
        for (int t = 0; t < r->animations[i].track_count; t++) {
            free(r->animations[i].tracks[t].keys);
        }
        free(r->animations[i].tracks);
    }
    free(r->animations);
    memset(r, 0, sizeof(*r));
}

/* ============================ Mesh triangulation ========================= */

/* Top-N influence picker: takes the cluster_index/weight pairs for one
 * control point and writes the largest N (by weight) into `out_bone` /
 * `out_weight`, renormalized so they sum to 1. Unused slots get bone=-1,
 * weight=0. */
static void pick_top_influences(const ufbx_skin_weight *wbeg, uint32_t wcount,
                                int32_t out_bone[SCENE_SKIN_INFLUENCES_PER_VERTEX],
                                float   out_weight[SCENE_SKIN_INFLUENCES_PER_VERTEX]) {
    for (int k = 0; k < SCENE_SKIN_INFLUENCES_PER_VERTEX; k++) {
        out_bone[k]   = -1;
        out_weight[k] = 0.0f;
    }
    /* ufbx weights are sorted by decreasing weight already, so just take
     * the first N. (Per ufbx_skin_vertex docstring.) */
    int n = (int)wcount;
    if (n > SCENE_SKIN_INFLUENCES_PER_VERTEX) n = SCENE_SKIN_INFLUENCES_PER_VERTEX;
    float sum = 0.0f;
    for (int k = 0; k < n; k++) {
        out_bone[k]   = (int32_t)wbeg[k].cluster_index;
        out_weight[k] = (float)wbeg[k].weight;
        sum += out_weight[k];
    }
    if (sum > 0.0f) {
        float inv = 1.0f / sum;
        for (int k = 0; k < n; k++) out_weight[k] *= inv;
    }
}

/* Build a scene_skin from a ufbx_skin_deformer for ONE material part.
 * `cp_for_corner` maps emitted-vertex index → control point index (length
 * vcount). Stashes per-bone ufbx_node* into `out_bone_nodes` (caller-
 * allocated, length skin->clusters.count) for later resolution against
 * the global node_map. Sets bone_node_index = -1 in every bone. Returns
 * 1 on success, 0 on alloc failure (caller frees outputs). */
static int build_skin_for_part(const ufbx_skin_deformer *sd,
                               const uint32_t *cp_for_corner,
                               int vcount,
                               const ufbx_vec3 *rest_pos_buf,
                               const ufbx_vec3 *rest_nrm_buf,
                               int owning_node_index,
                               scene_skin *out_skin,
                               const ufbx_node **out_bone_nodes) {
    memset(out_skin, 0, sizeof(*out_skin));
    out_skin->owning_node_index = owning_node_index;

    int bone_count = (int)sd->clusters.count;
    if (bone_count <= 0) return 0;

    out_skin->bones = calloc((size_t)bone_count, sizeof(scene_skin_bone));
    if (!out_skin->bones) return 0;
    out_skin->bone_count = bone_count;
    for (int b = 0; b < bone_count; b++) {
        const ufbx_skin_cluster *cl = sd->clusters.data[b];
        out_skin->bones[b].bone_node_index = -1;          /* resolved later */
        out_skin->bones[b].bind_inv = mat4_from_ufbx(cl->geometry_to_bone);
        out_bone_nodes[b] = cl->bone_node;                /* may be NULL */
    }

    out_skin->vertex_count = vcount;
    out_skin->influences     = malloc(sizeof(scene_skin_vertex) * (size_t)vcount);
    out_skin->rest_positions = malloc(sizeof(vector) * (size_t)vcount);
    out_skin->rest_normals   = malloc(sizeof(vector) * (size_t)vcount);
    if (!out_skin->influences || !out_skin->rest_positions || !out_skin->rest_normals) {
        return 0;
    }

    for (int i = 0; i < vcount; i++) {
        out_skin->rest_positions[i] = vec3_from_ufbx(rest_pos_buf[i]);
        out_skin->rest_normals[i]   = vec3_from_ufbx(rest_nrm_buf[i]);

        uint32_t cp = cp_for_corner[i];
        if (cp >= sd->vertices.count) {
            out_skin->influences[i].bone[0] = -1;
            out_skin->influences[i].weight[0] = 0.0f;
            for (int k = 1; k < SCENE_SKIN_INFLUENCES_PER_VERTEX; k++) {
                out_skin->influences[i].bone[k]   = -1;
                out_skin->influences[i].weight[k] = 0.0f;
            }
            continue;
        }
        const ufbx_skin_vertex sv = sd->vertices.data[cp];
        const ufbx_skin_weight *wbeg = (sv.num_weights > 0)
            ? &sd->weights.data[sv.weight_begin] : NULL;
        pick_top_influences(wbeg, sv.num_weights,
                            out_skin->influences[i].bone,
                            out_skin->influences[i].weight);
    }
    return 1;
}

/* Per-skin scratch held during scene_load_fbx so we can defer cluster→
 * scene_node resolution until after the full DFS populates node_map. */
typedef struct {
    const ufbx_node **bone_nodes;   /* length == out->skins[skin_idx].bone_count */
    int               bone_count;
} skin_scratch_entry;

/* Appends one scene_mesh per ufbx_mesh_part into `out->meshes`, and one
 * paired scene_skin per emitted mesh when the source has a skin deformer.
 * Returns the scene_mesh index of the FIRST part emitted (-1 if none),
 * or -2 on allocation failure.
 *
 * `owning_node_base` is the scene_node index that will own the FIRST
 * emitted material part (the parent node added right after this returns).
 * Subsequent material parts are owned by child nodes inserted at
 * owning_node_base + 1, +2, ... — we predict that arrangement here so
 * each skin's owning_node_index lands on the right node without
 * requiring a fixup pass.
 *
 * `scratch` and `*scratch_count` / `*scratch_capacity` track per-skin
 * ufbx_node** lists for deferred bone resolution. Grown in lockstep
 * with out->skins. */
static int emit_meshes_for_part_set(const ufbx_mesh *mesh,
                                    const int *mat_map,
                                    int default_material_index,
                                    int owning_node_base,
                                    scene_fbx_result *out,
                                    int *out_mesh_capacity,
                                    int *out_skin_capacity,
                                    skin_scratch_entry **scratch,
                                    int *scratch_capacity) {
    /* First skin deformer wins; warn on extras. */
    const ufbx_skin_deformer *sd = NULL;
    if (mesh->skin_deformers.count > 0) {
        sd = mesh->skin_deformers.data[0];
        if (mesh->skin_deformers.count > 1) {
            fprintf(stderr,
                    "scene_load_fbx: mesh \"%.*s\" has %zu skin deformers; "
                    "blending multiple skins is not supported, using the first.\n",
                    (int)mesh->name.length, mesh->name.data,
                    mesh->skin_deformers.count);
        }
    }

    int first_emitted = -1;
    int part_emit_idx = 0;  /* index of THIS emitted part within this mesh */
    for (size_t p = 0; p < mesh->material_parts.count; p++) {
        const ufbx_mesh_part *part = &mesh->material_parts.data[p];
        if (part->num_triangles == 0) continue;

        size_t tri_cap = mesh->max_face_triangles;
        uint32_t *tri_scratch = malloc(sizeof(uint32_t) * tri_cap * 3);
        if (!tri_scratch) return -2;

        size_t vcap = part->num_triangles * 3;
        scene_vertex *verts = malloc(sizeof(scene_vertex) * vcap);
        uint32_t     *inds  = malloc(sizeof(uint32_t)     * vcap);
        /* Skin scratch parallels `verts` so we can record per-corner data
         * for skin construction. Allocated only when the source mesh has
         * a skin. */
        uint32_t  *cp_for_corner = NULL;
        ufbx_vec3 *rest_pos_buf  = NULL;
        ufbx_vec3 *rest_nrm_buf  = NULL;
        if (sd) {
            cp_for_corner = malloc(sizeof(uint32_t)  * vcap);
            rest_pos_buf  = malloc(sizeof(ufbx_vec3) * vcap);
            rest_nrm_buf  = malloc(sizeof(ufbx_vec3) * vcap);
        }
        if (!verts || !inds || (sd && (!cp_for_corner || !rest_pos_buf || !rest_nrm_buf))) {
            free(tri_scratch); free(verts); free(inds);
            free(cp_for_corner); free(rest_pos_buf); free(rest_nrm_buf);
            return -2;
        }

        int vcount = 0, icount = 0;
        for (size_t fi = 0; fi < part->face_indices.count; fi++) {
            ufbx_face face = mesh->faces.data[part->face_indices.data[fi]];
            uint32_t ntri = ufbx_triangulate_face(tri_scratch, tri_cap * 3,
                                                  mesh, face);
            for (uint32_t t = 0; t < ntri; t++) {
                for (int k = 0; k < 3; k++) {
                    uint32_t vi = tri_scratch[t * 3 + k];
                    scene_vertex v;
                    ufbx_vec3 raw_pos = ufbx_get_vertex_vec3(
                        &mesh->vertex_position, vi);
                    v.position = vec3_from_ufbx(raw_pos);
                    ufbx_vec3 raw_nrm;
                    if (mesh->vertex_normal.exists) {
                        raw_nrm = ufbx_get_vertex_vec3(
                            &mesh->vertex_normal, vi);
                        v.normal = vec3_from_ufbx(raw_nrm);
                    } else {
                        raw_nrm = (ufbx_vec3){0, 1, 0};
                        v.normal = (vector){0, 1, 0};
                    }
                    if (mesh->vertex_uv.exists) {
                        ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, vi);
                        v.u = (float)uv.x;
                        v.v = (float)uv.y;
                    } else {
                        v.u = v.v = 0.0f;
                    }
                    verts[vcount] = v;
                    inds[icount++] = (uint32_t)vcount;
                    if (sd) {
                        /* mesh->vertex_indices maps per-corner index to
                         * control point index; weights are indexed by CP. */
                        uint32_t cp = (vi < mesh->vertex_indices.count)
                            ? mesh->vertex_indices.data[vi]
                            : 0;
                        cp_for_corner[vcount] = cp;
                        rest_pos_buf[vcount]  = raw_pos;
                        rest_nrm_buf[vcount]  = raw_nrm;
                    }
                    vcount++;
                }
            }
        }
        free(tri_scratch);

        if (vcount == 0) {
            free(verts); free(inds);
            free(cp_for_corner); free(rest_pos_buf); free(rest_nrm_buf);
            continue;
        }

        /* Resolve material: part->index maps to mesh->materials[part->index]
         * (see ufbx docs). That material pointer -> our material index. */
        int matidx = default_material_index;
        if (part->index < mesh->materials.count) {
            const ufbx_material *m = mesh->materials.data[part->index];
            if (m && (uint32_t)m->typed_id != UFBX_NO_INDEX) {
                matidx = mat_map[m->typed_id];
            }
        }

        /* Grow output meshes array. */
        if (out->mesh_count >= *out_mesh_capacity) {
            int nc = *out_mesh_capacity ? *out_mesh_capacity * 2 : 8;
            scene_mesh *nm = realloc(out->meshes, sizeof(scene_mesh) * nc);
            if (!nm) {
                free(verts); free(inds);
                free(cp_for_corner); free(rest_pos_buf); free(rest_nrm_buf);
                return -2;
            }
            out->meshes = nm;
            *out_mesh_capacity = nc;
        }

        scene_mesh sm;
        memset(&sm, 0, sizeof(sm));
        sm.vertices       = verts;
        sm.vertex_count   = vcount;
        sm.indices        = inds;
        sm.index_count    = icount;
        sm.material_index = matidx;
        sm.skin_index     = -1;
        scene_mesh_compute_bounds(&sm);

        /* If the source has a skin, build one scene_skin per emitted
         * scene_mesh. Each part shares the cluster set but has its own
         * rest pose / per-vertex influences. owning_node_index is
         * predicted from the DFS layout: parent at owning_node_base,
         * extras at owning_node_base + 1, +2, ... */
        if (sd) {
            if (out->skin_count >= *out_skin_capacity) {
                int nc = *out_skin_capacity ? *out_skin_capacity * 2 : 4;
                scene_skin *ns = realloc(out->skins, sizeof(scene_skin) * nc);
                skin_scratch_entry *nsc = realloc(*scratch,
                                                  sizeof(skin_scratch_entry) * nc);
                if (!ns || !nsc) {
                    free(ns); free(nsc);
                    free(verts); free(inds);
                    free(cp_for_corner); free(rest_pos_buf); free(rest_nrm_buf);
                    return -2;
                }
                out->skins = ns;
                *scratch = nsc;
                *out_skin_capacity = nc;
                *scratch_capacity = nc;
            }

            ufbx_node **bone_nodes_arr =
                malloc(sizeof(ufbx_node*) * sd->clusters.count);
            if (!bone_nodes_arr) {
                free(verts); free(inds);
                free(cp_for_corner); free(rest_pos_buf); free(rest_nrm_buf);
                return -2;
            }

            scene_skin sk;
            int ok = build_skin_for_part(sd, cp_for_corner, vcount,
                                         rest_pos_buf, rest_nrm_buf,
                                         owning_node_base + part_emit_idx,
                                         &sk,
                                         (const ufbx_node **)bone_nodes_arr);
            if (!ok) {
                free(sk.bones); free(sk.influences);
                free(sk.rest_positions); free(sk.rest_normals);
                free(bone_nodes_arr);
                free(verts); free(inds);
                free(cp_for_corner); free(rest_pos_buf); free(rest_nrm_buf);
                return -2;
            }

            sm.skin_index = out->skin_count;
            out->skins[out->skin_count] = sk;
            (*scratch)[out->skin_count].bone_nodes =
                (const ufbx_node **)bone_nodes_arr;
            (*scratch)[out->skin_count].bone_count = sk.bone_count;
            out->skin_count++;
        }

        free(cp_for_corner); free(rest_pos_buf); free(rest_nrm_buf);

        if (first_emitted < 0) first_emitted = out->mesh_count;
        out->meshes[out->mesh_count++] = sm;
        part_emit_idx++;
    }
    return first_emitted;
}

/* ============================== Animation ================================ */

/* Epsilon for detecting "this channel is effectively constant" — in both
 * radians and world units, 1e-4 is well below visible noise. */
#define ANIM_CHANNEL_EPS 1e-4f

static int emit_animation(const ufbx_anim_stack *stack,
                          const ufbx_scene *uscene,
                          const int *node_map,
                          scene_animation *out) {
    memset(out, 0, sizeof(*out));
    size_t nlen = stack->name.length < sizeof(out->name) - 1
                  ? stack->name.length : sizeof(out->name) - 1;
    memcpy(out->name, stack->name.data, nlen);
    out->name[nlen] = '\0';

    double t0 = stack->time_begin;
    double t1 = stack->time_end;
    if (t1 <= t0) { out->duration = 0.0f; return 0; }
    out->duration = (float)(t1 - t0);

    double dt = 1.0 / (double)SCENE_FBX_BAKE_HZ;
    int    nframes = (int)((t1 - t0) / dt + 0.5) + 1;
    if (nframes < 2) nframes = 2;

    int track_cap = 64;
    out->tracks = malloc(sizeof(scene_anim_track) * track_cap);
    if (!out->tracks) return -1;
    out->track_count = 0;

    float *samples = malloc(sizeof(float) * 9 * nframes);
    if (!samples) { free(out->tracks); out->tracks = NULL; return -1; }

    for (size_t nid = 0; nid < uscene->nodes.count; nid++) {
        int our_idx = node_map[nid];
        if (our_idx < 0) continue;

        const ufbx_node *n = uscene->nodes.data[nid];
        /* Sample all 9 channels across the clip. */
        for (int f = 0; f < nframes; f++) {
            double tt = t0 + (double)f * dt;
            if (f == nframes - 1) tt = t1;  /* pin last frame */
            ufbx_transform tr = ufbx_evaluate_transform(stack->anim, n, tt);
            ufbx_vec3 e = ufbx_quat_to_euler(tr.rotation, UFBX_ROTATION_ORDER_XYZ);
            samples[0 * nframes + f] = (float)tr.translation.x;
            samples[1 * nframes + f] = (float)tr.translation.y;
            samples[2 * nframes + f] = (float)tr.translation.z;
            samples[3 * nframes + f] = (float)e.x * UFBX_DEG_TO_RAD;
            samples[4 * nframes + f] = (float)e.y * UFBX_DEG_TO_RAD;
            samples[5 * nframes + f] = (float)e.z * UFBX_DEG_TO_RAD;
            samples[6 * nframes + f] = (float)tr.scale.x;
            samples[7 * nframes + f] = (float)tr.scale.y;
            samples[8 * nframes + f] = (float)tr.scale.z;
        }

        for (int ch = 0; ch < 9; ch++) {
            float *col = &samples[ch * nframes];
            float mn = col[0], mx = col[0];
            for (int f = 1; f < nframes; f++) {
                if (col[f] < mn) mn = col[f];
                if (col[f] > mx) mx = col[f];
            }
            if (mx - mn < ANIM_CHANNEL_EPS) continue;  /* constant channel */

            if (out->track_count >= track_cap) {
                int nc = track_cap * 2;
                scene_anim_track *nt = realloc(out->tracks,
                                               sizeof(scene_anim_track) * nc);
                if (!nt) goto oom;
                out->tracks = nt;
                track_cap = nc;
            }
            scene_anim_key *keys = malloc(sizeof(scene_anim_key) * nframes);
            if (!keys) goto oom;
            for (int f = 0; f < nframes; f++) {
                double tt = (f == nframes - 1) ? (t1 - t0)
                                               : ((double)f * dt);
                keys[f].time = (float)tt;
                keys[f].value = col[f];
            }
            scene_anim_track *track = &out->tracks[out->track_count++];
            track->node_index = our_idx;
            track->channel    = (scene_anim_channel)ch;
            track->keys       = keys;
            track->key_count  = nframes;
        }
    }
    free(samples);
    return 0;

oom:
    free(samples);
    for (int i = 0; i < out->track_count; i++) free(out->tracks[i].keys);
    free(out->tracks);
    out->tracks = NULL;
    out->track_count = 0;
    return -1;
}

/* ============================== Main loader ============================== */

/* Load the FBX via ufbx with our project-wide options. Returns a ufbx
 * scene on success, NULL on failure (with an error already printed).
 * Skinned meshes are loaded as scene_skin records — see scene_apply_skinning. */
static ufbx_scene *load_and_gate(const char *path, scene_fbx_flags flags) {
    ufbx_load_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.generate_missing_normals = true;
    opts.target_axes              = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters       = 1.0;
    if (flags & SCENE_FBX_SKIP_ANIMATION) opts.ignore_animation = true;
    if (flags & SCENE_FBX_ANIMATION_ONLY) opts.ignore_geometry  = true;

    ufbx_error err;
    ufbx_scene *u = ufbx_load_file(path, &opts, &err);
    if (!u) {
        char msg[512];
        ufbx_format_error(msg, sizeof(msg), &err);
        fprintf(stderr, "scene_load_fbx: %s\n", msg);
        return NULL;
    }
    return u;
}

int scene_load_fbx(const char *path, scene_fbx_flags flags,
                   scene_fbx_result *out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!path) return 0;
    if (flags & SCENE_FBX_ANIMATION_ONLY) {
        fprintf(stderr, "scene_load_fbx: SCENE_FBX_ANIMATION_ONLY is "
                        "only valid with scene_add_fbx.\n");
        return 0;
    }

    ufbx_scene *u = load_and_gate(path, flags);
    if (!u) return 0;

    /* Convert materials first — node/mesh emission needs the map. */
    int *mat_map = NULL;
    if (u->materials.count > 0) {
        mat_map = malloc(sizeof(int) * u->materials.count);
        if (!mat_map) goto oom;
        out->materials = malloc(sizeof(scene_material) * u->materials.count);
        if (!out->materials) goto oom;
        for (size_t i = 0; i < u->materials.count; i++) {
            out->materials[out->material_count] =
                convert_material(u->materials.data[i]);
            mat_map[i] = out->material_count++;
        }
    }

    int default_material_index = -1;
    if (out->material_count == 0) {
        out->materials = malloc(sizeof(scene_material));
        if (!out->materials) goto oom;
        out->materials[0] = scene_material_default();
        out->material_count = 1;
        default_material_index = 0;
    } else {
        default_material_index = 0;  /* fallback to first material */
    }

    /* Convert nodes — DFS from root so parents precede children. */
    int *node_map = malloc(sizeof(int) * u->nodes.count);
    if (!node_map) goto oom;
    for (size_t i = 0; i < u->nodes.count; i++) node_map[i] = -1;

    out->nodes = malloc(sizeof(scene_node) * (u->nodes.count + 8));
    if (!out->nodes) { free(node_map); goto oom; }

    int mesh_cap = 0;
    int skin_cap = 0;
    skin_scratch_entry *skin_scratch = NULL;
    int skin_scratch_cap = 0;

    /* Iterative DFS using a stack of ufbx_node pointers. We skip the
     * root itself but visit its direct children as top-level nodes. */
    const ufbx_node **stack = malloc(sizeof(*stack) * u->nodes.count);
    if (!stack) { free(node_map); goto oom; }
    int sp = 0;
    for (size_t i = u->root_node->children.count; i-- > 0; ) {
        stack[sp++] = u->root_node->children.data[i];
    }
    while (sp > 0) {
        const ufbx_node *n = stack[--sp];
        int parent_idx = -1;
        if (n->parent && n->parent != u->root_node) {
            parent_idx = node_map[n->parent->typed_id];
        }

        /* Emit meshes for this node (per material part). The owning_node
         * for the first part is the parent node we're about to add at
         * out->node_count; subsequent parts go to children at
         * out->node_count + 1, +2, ... */
        int first_mesh = -1;
        if (n->mesh) {
            int owning_node_base = out->node_count;
            first_mesh = emit_meshes_for_part_set(
                n->mesh, mat_map, default_material_index,
                owning_node_base,
                out, &mesh_cap, &skin_cap,
                &skin_scratch, &skin_scratch_cap);
            if (first_mesh == -2) {
                free(stack); free(node_map);
                for (int i = 0; i < out->skin_count; i++) free(skin_scratch[i].bone_nodes);
                free(skin_scratch);
                goto oom;
            }
        }

        scene_node sn;
        memset(sn.name, 0, sizeof(sn.name));
        size_t nlen = n->name.length < sizeof(sn.name) - 1
                      ? n->name.length : sizeof(sn.name) - 1;
        memcpy(sn.name, n->name.data, nlen);
        sn.transform    = transform_from_ufbx(n->local_transform);
        sn.parent_index = parent_idx;
        sn.mesh_index   = first_mesh;  /* -1 if no mesh */
        int our_idx = out->node_count;
        out->nodes[out->node_count++] = sn;
        node_map[n->typed_id] = our_idx;

        /* If the mesh had extra material parts, attach them as identity
         * children so the tree stays "one mesh per node". */
        if (n->mesh && first_mesh >= 0) {
            int total_parts = 0;
            for (size_t p = 0; p < n->mesh->material_parts.count; p++) {
                if (n->mesh->material_parts.data[p].num_triangles > 0) {
                    total_parts++;
                }
            }
            for (int extra = 1; extra < total_parts; extra++) {
                scene_node child;
                memset(child.name, 0, sizeof(child.name));
                child.transform    = scene_transform_identity();
                child.parent_index = our_idx;
                child.mesh_index   = first_mesh + extra;
                out->nodes[out->node_count++] = child;
            }
        }

        /* Push children (reverse order so iteration is stable left-to-right). */
        for (size_t i = n->children.count; i-- > 0; ) {
            stack[sp++] = n->children.data[i];
        }
    }
    free(stack);

    /* Resolve skin cluster bone_node pointers to scene_node indices via
     * node_map, now that the DFS is complete. Bones whose ufbx_node
     * pointer is NULL or maps to -1 are kept with bone_node_index = -1;
     * scene_apply_skinning treats those as identity transforms. */
    for (int si = 0; si < out->skin_count; si++) {
        scene_skin *sk = &out->skins[si];
        const skin_scratch_entry *sc = &skin_scratch[si];
        for (int b = 0; b < sk->bone_count && b < sc->bone_count; b++) {
            const ufbx_node *bn = sc->bone_nodes[b];
            if (bn && bn != u->root_node) {
                int our = node_map[bn->typed_id];
                sk->bones[b].bone_node_index = our;  /* may be -1 if unmapped */
            }
        }
    }
    for (int i = 0; i < out->skin_count; i++) free(skin_scratch[i].bone_nodes);
    free(skin_scratch);

    /* Animations. */
    if (!(flags & SCENE_FBX_SKIP_ANIMATION) && u->anim_stacks.count > 0) {
        out->animations = malloc(sizeof(scene_animation) * u->anim_stacks.count);
        if (!out->animations) { free(node_map); goto oom; }
        for (size_t i = 0; i < u->anim_stacks.count; i++) {
            scene_animation anim;
            if (emit_animation(u->anim_stacks.data[i], u, node_map,
                               &anim) != 0) {
                free(node_map); goto oom;
            }
            if (anim.track_count > 0) {
                out->animations[out->animation_count++] = anim;
            } else {
                /* Empty — free the empty tracks buffer. */
                free(anim.tracks);
            }
        }
    }

    free(node_map);
    free(mat_map);
    ufbx_free_scene(u);
    return 1;

oom:
    free(mat_map);
    ufbx_free_scene(u);
    scene_fbx_result_free(out);
    return 0;
}

/* ============================= Append to scene =========================== */

/* Animation-only import: load just the anim_stacks from the FBX and bind
 * each track's node_index to an existing scene node by name match. */
static int add_fbx_animations_only(scene *s, const char *path,
                                   scene_fbx_flags flags) {
    ufbx_scene *u = load_and_gate(path, flags);
    if (!u) return -1;
    if (u->anim_stacks.count == 0) {
        ufbx_free_scene(u);
        return 0;
    }

    int *node_map = malloc(sizeof(int) * u->nodes.count);
    if (!node_map) { ufbx_free_scene(u); return -1; }

    int unmatched = 0;
    for (size_t i = 0; i < u->nodes.count; i++) {
        const ufbx_node *n = u->nodes.data[i];
        if (n == u->root_node || n->name.length == 0) {
            node_map[i] = -1;
            continue;
        }
        /* ufbx_string isn't NUL-terminated — copy to a local. */
        char buf[64];
        size_t nlen = n->name.length < sizeof(buf) - 1
                      ? n->name.length : sizeof(buf) - 1;
        memcpy(buf, n->name.data, nlen);
        buf[nlen] = '\0';
        int idx = scene_find_node_by_name(s, buf);
        node_map[i] = idx;
        if (idx < 0) unmatched++;
    }
    if (unmatched > 0) {
        fprintf(stderr,
                "scene_add_fbx: %s: %d animated FBX node(s) had no "
                "matching scene node by name — those tracks will be dropped.\n",
                path, unmatched);
    }

    int added = 0;
    for (size_t i = 0; i < u->anim_stacks.count; i++) {
        scene_animation anim;
        if (emit_animation(u->anim_stacks.data[i], u, node_map, &anim) != 0) {
            free(node_map); ufbx_free_scene(u); return -1;
        }
        if (anim.track_count > 0) {
            if (scene_add_animation(s, anim) < 0) {
                for (int t = 0; t < anim.track_count; t++) free(anim.tracks[t].keys);
                free(anim.tracks);
                free(node_map); ufbx_free_scene(u); return -1;
            }
            added++;
        } else {
            free(anim.tracks);
        }
    }
    free(node_map);
    ufbx_free_scene(u);
    return added;
}

int scene_add_fbx(scene *s, const char *path, scene_fbx_flags flags,
                  int *first_node_index_out) {
    if (flags & SCENE_FBX_ANIMATION_ONLY) {
        if (first_node_index_out) *first_node_index_out = s->node_count;
        return add_fbx_animations_only(s, path, flags);
    }
    scene_fbx_result r;
    if (!scene_load_fbx(path, flags, &r)) return -1;

    int mat_base  = s->material_count;
    int skin_base = s->skin_count;
    int mesh_base = s->mesh_count;
    int node_base = s->node_count;
    if (first_node_index_out) *first_node_index_out = node_base;

    /* Materials (copy-by-value). */
    for (int i = 0; i < r.material_count; i++) {
        if (scene_add_material(s, r.materials[i]) < 0) goto fail;
    }
    /* Skins (move ownership of all owned buffers). Must be added BEFORE
     * meshes so scene_add_mesh's skin_index range check accepts the
     * mesh's rebased skin_index. Rebase bone/owning node indices to
     * post-append scene_node space. */
    for (int i = 0; i < r.skin_count; i++) {
        scene_skin sk = r.skins[i];
        if (sk.owning_node_index >= 0) sk.owning_node_index += node_base;
        for (int b = 0; b < sk.bone_count; b++) {
            if (sk.bones[b].bone_node_index >= 0) {
                sk.bones[b].bone_node_index += node_base;
            }
        }
        if (scene_add_skin(s, sk) < 0) {
            free(sk.bones); free(sk.influences);
            free(sk.rest_positions); free(sk.rest_normals);
            goto fail;
        }
        /* Ownership transferred — zero the source so result_free is safe. */
        r.skins[i].bones = NULL;
        r.skins[i].influences = NULL;
        r.skins[i].rest_positions = NULL;
        r.skins[i].rest_normals = NULL;
        r.skins[i].bone_count = 0;
        r.skins[i].vertex_count = 0;
    }
    /* Meshes (move ownership of vertices/indices). Rewrite material_index
     * and skin_index (the latter relative to r.skins; rebase to scene). */
    for (int i = 0; i < r.mesh_count; i++) {
        scene_mesh m = r.meshes[i];
        if (m.material_index >= 0) m.material_index += mat_base;
        if (m.skin_index     >= 0) m.skin_index     += skin_base;
        if (scene_add_mesh(s, m) < 0) {
            /* If the add fails, the mesh's buffers weren't taken — free here. */
            free(m.vertices); free(m.indices); free(m.accel);
            goto fail;
        }
        /* Ownership transferred — zero the source so result_free doesn't double-free. */
        r.meshes[i].vertices = NULL;
        r.meshes[i].indices  = NULL;
        r.meshes[i].accel    = NULL;
    }
    /* Nodes. Rewrite parent_index / mesh_index. */
    for (int i = 0; i < r.node_count; i++) {
        scene_node n = r.nodes[i];
        if (n.parent_index >= 0) n.parent_index += node_base;
        if (n.mesh_index   >= 0) n.mesh_index   += mesh_base;
        if (scene_add_node(s, n) < 0) goto fail;
    }
    /* Animations. Rewrite track node_index (move ownership of tracks). */
    for (int i = 0; i < r.animation_count; i++) {
        scene_animation a = r.animations[i];
        for (int t = 0; t < a.track_count; t++) {
            a.tracks[t].node_index += node_base;
        }
        if (scene_add_animation(s, a) < 0) {
            for (int t = 0; t < a.track_count; t++) free(a.tracks[t].keys);
            free(a.tracks);
            goto fail;
        }
        r.animations[i].tracks      = NULL;
        r.animations[i].track_count = 0;
    }

    int added = r.node_count;
    scene_fbx_result_free(&r);
    return added;

fail:
    scene_fbx_result_free(&r);
    return -1;
}
