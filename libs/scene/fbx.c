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

static scene_transform transform_from_ufbx(ufbx_transform t) {
    ufbx_vec3 e = ufbx_quat_to_euler(t.rotation, UFBX_ROTATION_ORDER_XYZ);
    scene_transform out;
    out.position = vec3_from_ufbx(t.translation);
    out.rotation = (vector){ (float)e.x, (float)e.y, (float)e.z };
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

/* Appends one scene_mesh per ufbx_mesh_part into `out->meshes`. Returns
 * the scene_mesh index of the FIRST part emitted (-1 if none), or -2 on
 * allocation failure. */
static int emit_meshes_for_part_set(const ufbx_mesh *mesh,
                                    const int *mat_map,
                                    int default_material_index,
                                    scene_fbx_result *out,
                                    int *out_mesh_capacity) {
    int first_emitted = -1;
    for (size_t p = 0; p < mesh->material_parts.count; p++) {
        const ufbx_mesh_part *part = &mesh->material_parts.data[p];
        if (part->num_triangles == 0) continue;

        size_t tri_cap = mesh->max_face_triangles;
        uint32_t *tri_scratch = malloc(sizeof(uint32_t) * tri_cap * 3);
        if (!tri_scratch) return -2;

        size_t vcap = part->num_triangles * 3;
        scene_vertex *verts = malloc(sizeof(scene_vertex) * vcap);
        uint32_t     *inds  = malloc(sizeof(uint32_t)     * vcap);
        if (!verts || !inds) {
            free(tri_scratch); free(verts); free(inds);
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
                    v.position = vec3_from_ufbx(
                        ufbx_get_vertex_vec3(&mesh->vertex_position, vi));
                    if (mesh->vertex_normal.exists) {
                        v.normal = vec3_from_ufbx(
                            ufbx_get_vertex_vec3(&mesh->vertex_normal, vi));
                    } else {
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
                    vcount++;
                }
            }
        }
        free(tri_scratch);

        if (vcount == 0) {
            free(verts); free(inds);
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
            if (!nm) { free(verts); free(inds); return -2; }
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
        scene_mesh_compute_bounds(&sm);

        if (first_emitted < 0) first_emitted = out->mesh_count;
        out->meshes[out->mesh_count++] = sm;
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
            samples[3 * nframes + f] = (float)e.x;
            samples[4 * nframes + f] = (float)e.y;
            samples[5 * nframes + f] = (float)e.z;
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

/* Load the FBX via ufbx and run the skinned-mesh gate. Returns a ufbx
 * scene on success, NULL on failure (with an error already printed). */
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

    /* Skinning gate only matters when meshes are being consumed. */
    if (!(flags & (SCENE_FBX_ALLOW_SKINNED | SCENE_FBX_ANIMATION_ONLY))) {
        for (size_t i = 0; i < u->meshes.count; i++) {
            if (u->meshes.data[i]->skin_deformers.count > 0) {
                fprintf(stderr,
                    "scene_load_fbx: %s contains skinned mesh \"%.*s\"; "
                    "re-export with rigid parenting or pass "
                    "SCENE_FBX_ALLOW_SKINNED.\n",
                    path,
                    (int)u->meshes.data[i]->name.length,
                    u->meshes.data[i]->name.data);
                ufbx_free_scene(u);
                return NULL;
            }
        }
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

        /* Emit meshes for this node (per material part). */
        int first_mesh = -1;
        if (n->mesh) {
            first_mesh = emit_meshes_for_part_set(
                n->mesh, mat_map, default_material_index,
                out, &mesh_cap);
            if (first_mesh == -2) {
                free(stack); free(node_map); goto oom;
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
    int mesh_base = s->mesh_count;
    int node_base = s->node_count;
    if (first_node_index_out) *first_node_index_out = node_base;

    /* Materials (copy-by-value). */
    for (int i = 0; i < r.material_count; i++) {
        if (scene_add_material(s, r.materials[i]) < 0) goto fail;
    }
    /* Meshes (move ownership of vertices/indices). Rewrite material_index. */
    for (int i = 0; i < r.mesh_count; i++) {
        scene_mesh m = r.meshes[i];
        if (m.material_index >= 0) m.material_index += mat_base;
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
