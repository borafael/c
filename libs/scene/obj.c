#include "obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    vector *data;
    int     count;
    int     capacity;
} vec3_arr;

typedef struct {
    float *data;    /* u, v pairs */
    int    count;   /* number of pairs */
    int    capacity;
} vec2_arr;

typedef struct {
    scene_vertex *data;
    int           count;
    int           capacity;
} vertex_arr;

typedef struct {
    uint32_t *data;
    int       count;
    int       capacity;
} index_arr;

static int vec3_push(vec3_arr *a, vector v) {
    if (a->count == a->capacity) {
        int cap = a->capacity ? a->capacity * 2 : 64;
        vector *nd = realloc(a->data, sizeof(vector) * cap);
        if (!nd) return 0;
        a->data = nd;
        a->capacity = cap;
    }
    a->data[a->count++] = v;
    return 1;
}

static int vec2_push(vec2_arr *a, float u, float v) {
    if (a->count == a->capacity) {
        int cap = a->capacity ? a->capacity * 2 : 64;
        float *nd = realloc(a->data, sizeof(float) * 2 * cap);
        if (!nd) return 0;
        a->data = nd;
        a->capacity = cap;
    }
    a->data[a->count * 2 + 0] = u;
    a->data[a->count * 2 + 1] = v;
    a->count++;
    return 1;
}

static int vertex_push(vertex_arr *a, scene_vertex v) {
    if (a->count == a->capacity) {
        int cap = a->capacity ? a->capacity * 2 : 128;
        scene_vertex *nd = realloc(a->data, sizeof(scene_vertex) * cap);
        if (!nd) return 0;
        a->data = nd;
        a->capacity = cap;
    }
    a->data[a->count++] = v;
    return 1;
}

static int index_push(index_arr *a, uint32_t v) {
    if (a->count == a->capacity) {
        int cap = a->capacity ? a->capacity * 2 : 256;
        uint32_t *nd = realloc(a->data, sizeof(uint32_t) * cap);
        if (!nd) return 0;
        a->data = nd;
        a->capacity = cap;
    }
    a->data[a->count++] = v;
    return 1;
}

/* Resolve a 1-based OBJ index against a current array size.
 * OBJ also allows negative indices (offset from end: -1 is the last). */
static int resolve_index(int raw, int array_size) {
    if (raw > 0) return raw - 1;
    if (raw < 0) return array_size + raw;
    return -1;
}

/* Parse one face-vertex token like "3", "3/5", "3//7", "3/5/7".
 * vt_out / vn_out are set to 0 when absent (0 is "none" in OBJ's 1-based
 * space, so we can distinguish). */
static int parse_face_vertex(const char *tok, int *v_out, int *vt_out, int *vn_out) {
    *v_out = 0; *vt_out = 0; *vn_out = 0;
    char *end;
    long a = strtol(tok, &end, 10);
    if (end == tok) return 0;
    *v_out = (int)a;
    if (*end != '/') return 1;
    const char *p = end + 1;
    if (*p == '/') {
        /* v//vn */
        long c = strtol(p + 1, &end, 10);
        if (end == p + 1) return 1;
        *vn_out = (int)c;
        return 1;
    }
    long b = strtol(p, &end, 10);
    if (end == p) return 1;
    *vt_out = (int)b;
    if (*end != '/') return 1;
    long c = strtol(end + 1, &end, 10);
    *vn_out = (int)c;
    return 1;
}

static void free_arrays(vec3_arr *positions, vec3_arr *normals,
                       vec2_arr *uvs, vertex_arr *verts, index_arr *idx) {
    free(positions->data);
    free(normals->data);
    free(uvs->data);
    free(verts->data);
    free(idx->data);
}

int scene_load_obj(const char *path, int material_index, scene_mesh *out) {
    if (!path || !out) return 0;
    memset(out, 0, sizeof(*out));
    out->material_index = material_index;

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    vec3_arr positions = {0};
    vec3_arr normals   = {0};
    vec2_arr uvs       = {0};
    vertex_arr verts   = {0};
    index_arr  indices = {0};

    char line[1024];
    int ok = 1;

    while (ok && fgets(line, sizeof(line), f)) {
        /* Strip comments and trailing newline. */
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '\r') continue;

        if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            vector v;
            if (sscanf(p + 1, "%f %f %f", &v.x, &v.y, &v.z) == 3) {
                if (!vec3_push(&positions, v)) { ok = 0; break; }
            }
        } else if (p[0] == 'v' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
            vector n;
            if (sscanf(p + 2, "%f %f %f", &n.x, &n.y, &n.z) == 3) {
                if (!vec3_push(&normals, n)) { ok = 0; break; }
            }
        } else if (p[0] == 'v' && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) {
            float u = 0.0f, vv = 0.0f;
            sscanf(p + 2, "%f %f", &u, &vv);
            if (!vec2_push(&uvs, u, vv)) { ok = 0; break; }
        } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            /* Collect all face vertices on this line, then fan-triangulate. */
            int face_base = verts.count;
            int face_n = 0;

            char *tok = strtok(p + 1, " \t\r\n");
            while (tok) {
                int vi = 0, ti = 0, ni = 0;
                if (parse_face_vertex(tok, &vi, &ti, &ni)) {
                    int pi_idx = resolve_index(vi, positions.count);
                    int ti_idx = (ti == 0) ? -1 : resolve_index(ti, uvs.count);
                    int ni_idx = (ni == 0) ? -1 : resolve_index(ni, normals.count);

                    scene_vertex sv = {0};
                    if (pi_idx >= 0 && pi_idx < positions.count) {
                        sv.position = positions.data[pi_idx];
                    }
                    if (ni_idx >= 0 && ni_idx < normals.count) {
                        sv.normal = normals.data[ni_idx];
                    }
                    if (ti_idx >= 0 && ti_idx < uvs.count) {
                        sv.u = uvs.data[ti_idx * 2 + 0];
                        sv.v = uvs.data[ti_idx * 2 + 1];
                    }
                    if (!vertex_push(&verts, sv)) { ok = 0; break; }
                    face_n++;
                }
                tok = strtok(NULL, " \t\r\n");
            }
            if (!ok) break;
            if (face_n < 3) continue;

            /* Fan triangulation: (0, k, k+1) for k = 1 .. face_n-2. */
            for (int k = 1; k < face_n - 1; k++) {
                if (!index_push(&indices, (uint32_t)(face_base + 0)) ||
                    !index_push(&indices, (uint32_t)(face_base + k)) ||
                    !index_push(&indices, (uint32_t)(face_base + k + 1))) {
                    ok = 0;
                    break;
                }
            }
        }
        /* Everything else (g, o, s, usemtl, mtllib, ...) is ignored. */
    }

    fclose(f);

    if (!ok || indices.count == 0 || verts.count == 0) {
        free_arrays(&positions, &normals, &uvs, &verts, &indices);
        memset(out, 0, sizeof(*out));
        return 0;
    }

    /* Fill in face normals for any vertex that has a zero normal. We do this
     * per triangle so OBJ files without `vn` directives still render shaded. */
    int tri_count = indices.count / 3;
    for (int t = 0; t < tri_count; t++) {
        uint32_t i0 = indices.data[t * 3 + 0];
        uint32_t i1 = indices.data[t * 3 + 1];
        uint32_t i2 = indices.data[t * 3 + 2];
        vector p0 = verts.data[i0].position;
        vector p1 = verts.data[i1].position;
        vector p2 = verts.data[i2].position;
        vector fn = vector_normalize(
            vector_cross(vector_sub(p1, p0), vector_sub(p2, p0)));
        uint32_t tri_idx[3] = { i0, i1, i2 };
        for (int k = 0; k < 3; k++) {
            scene_vertex *sv = &verts.data[tri_idx[k]];
            float mag2 = sv->normal.x * sv->normal.x
                       + sv->normal.y * sv->normal.y
                       + sv->normal.z * sv->normal.z;
            if (mag2 < 1e-12f) sv->normal = fn;
        }
    }

    /* Free scratch arrays but keep the owned output buffers. */
    free(positions.data);
    free(normals.data);
    free(uvs.data);

    out->vertices       = verts.data;
    out->vertex_count   = verts.count;
    out->indices        = indices.data;
    out->index_count    = indices.count;
    out->material_index = material_index;
    scene_mesh_compute_bounds(out);
    return 1;
}

int scene_add_mesh_from_obj(scene *s, const char *path, int material_index) {
    scene_mesh m;
    if (!scene_load_obj(path, material_index, &m)) return -1;
    int idx = scene_add_mesh(s, m);
    if (idx < 0) {
        free(m.vertices);
        free(m.indices);
    }
    return idx;
}

/* ================================ MTL loader =============================== */

static float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static uint8_t to_byte(float x) { return (uint8_t)(clamp01(x) * 255.0f + 0.5f); }

int scene_load_mtl(const char *path, scene_mtl_entry **out_entries) {
    if (!path || !out_entries) return -1;
    *out_entries = NULL;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    scene_mtl_entry *entries = NULL;
    int count = 0, cap = 0;
    scene_mtl_entry *cur = NULL;
    char line[1024];

    while (fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '\r') continue;

        if (strncmp(p, "newmtl", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            if (count == cap) {
                cap = cap ? cap * 2 : 16;
                scene_mtl_entry *nd = realloc(entries, sizeof(*entries) * cap);
                if (!nd) { free(entries); fclose(f); return -1; }
                entries = nd;
            }
            memset(&entries[count], 0, sizeof(entries[count]));
            entries[count].material = scene_material_default();
            char name[64] = {0};
            sscanf(p + 7, " %63s", name);
            strncpy(entries[count].name, name, sizeof(entries[count].name) - 1);
            cur = &entries[count++];
        } else if (cur && p[0] == 'K' && p[1] == 'd' && (p[2] == ' ' || p[2] == '\t')) {
            float r = 0, g = 0, b = 0;
            if (sscanf(p + 3, "%f %f %f", &r, &g, &b) == 3) {
                cur->material.albedo.r = to_byte(r);
                cur->material.albedo.g = to_byte(g);
                cur->material.albedo.b = to_byte(b);
            }
        }
        /* Ka, Ks, Ns, d, Tr, illum, map_Kd, ... ignored for now. */
    }

    fclose(f);
    *out_entries = entries;
    return count;
}

/* ================================ multi-mesh OBJ =========================== */

typedef struct {
    char       name[64];
    vertex_arr verts;
    index_arr  indices;
} obj_group;

typedef struct {
    obj_group *data;
    int        count;
    int        capacity;
} group_arr;

static obj_group *group_find_or_create(group_arr *gs, const char *name) {
    for (int i = 0; i < gs->count; i++) {
        if (strcmp(gs->data[i].name, name) == 0) return &gs->data[i];
    }
    if (gs->count == gs->capacity) {
        int cap = gs->capacity ? gs->capacity * 2 : 4;
        obj_group *nd = realloc(gs->data, sizeof(obj_group) * cap);
        if (!nd) return NULL;
        gs->data = nd;
        gs->capacity = cap;
    }
    obj_group *g = &gs->data[gs->count++];
    memset(g, 0, sizeof(*g));
    strncpy(g->name, name, sizeof(g->name) - 1);
    return g;
}

static void groups_free(group_arr *gs) {
    for (int i = 0; i < gs->count; i++) {
        free(gs->data[i].verts.data);
        free(gs->data[i].indices.data);
    }
    free(gs->data);
}

/* Face-normal fill-in for any vertex whose stored normal is ~zero. */
static void fill_face_normals(scene_vertex *verts, int vertex_count,
                              const uint32_t *indices, int index_count) {
    int tri_count = index_count / 3;
    for (int t = 0; t < tri_count; t++) {
        uint32_t i0 = indices[t * 3 + 0];
        uint32_t i1 = indices[t * 3 + 1];
        uint32_t i2 = indices[t * 3 + 2];
        if ((int)i0 >= vertex_count || (int)i1 >= vertex_count || (int)i2 >= vertex_count) continue;
        vector p0 = verts[i0].position;
        vector p1 = verts[i1].position;
        vector p2 = verts[i2].position;
        vector fn = vector_normalize(vector_cross(vector_sub(p1, p0), vector_sub(p2, p0)));
        uint32_t tri_idx[3] = { i0, i1, i2 };
        for (int k = 0; k < 3; k++) {
            scene_vertex *sv = &verts[tri_idx[k]];
            float mag2 = sv->normal.x * sv->normal.x
                       + sv->normal.y * sv->normal.y
                       + sv->normal.z * sv->normal.z;
            if (mag2 < 1e-12f) sv->normal = fn;
        }
    }
}

int scene_add_meshes_from_obj(scene *s, const char *obj_path,
                              const scene_mtl_entry *mtl_entries, int mtl_count,
                              int default_material_index,
                              int *first_mesh_index_out) {
    if (!s || !obj_path) return -1;

    FILE *f = fopen(obj_path, "r");
    if (!f) return -1;

    vec3_arr  positions = {0};
    vec3_arr  normals   = {0};
    vec2_arr  uvs       = {0};
    group_arr groups    = {0};

    /* Default group (for faces before the first usemtl). */
    obj_group *current = group_find_or_create(&groups, "");
    int ok = (current != NULL);

    char line[1024];
    while (ok && fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '\r') continue;

        if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            vector v;
            if (sscanf(p + 1, "%f %f %f", &v.x, &v.y, &v.z) == 3) {
                if (!vec3_push(&positions, v)) { ok = 0; break; }
            }
        } else if (p[0] == 'v' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
            vector n;
            if (sscanf(p + 2, "%f %f %f", &n.x, &n.y, &n.z) == 3) {
                if (!vec3_push(&normals, n)) { ok = 0; break; }
            }
        } else if (p[0] == 'v' && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) {
            float u = 0, vv = 0;
            sscanf(p + 2, "%f %f", &u, &vv);
            if (!vec2_push(&uvs, u, vv)) { ok = 0; break; }
        } else if (strncmp(p, "usemtl", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            char name[64] = {0};
            sscanf(p + 7, " %63s", name);
            current = group_find_or_create(&groups, name);
            if (!current) { ok = 0; break; }
        } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            int face_base = current->verts.count;
            int face_n = 0;
            char *tok = strtok(p + 1, " \t\r\n");
            while (tok) {
                int vi = 0, ti = 0, ni = 0;
                if (parse_face_vertex(tok, &vi, &ti, &ni)) {
                    int pi_idx = resolve_index(vi, positions.count);
                    int ti_idx = (ti == 0) ? -1 : resolve_index(ti, uvs.count);
                    int ni_idx = (ni == 0) ? -1 : resolve_index(ni, normals.count);

                    scene_vertex sv = {0};
                    if (pi_idx >= 0 && pi_idx < positions.count) sv.position = positions.data[pi_idx];
                    if (ni_idx >= 0 && ni_idx < normals.count)   sv.normal   = normals.data[ni_idx];
                    if (ti_idx >= 0 && ti_idx < uvs.count) {
                        sv.u = uvs.data[ti_idx * 2 + 0];
                        sv.v = uvs.data[ti_idx * 2 + 1];
                    }
                    if (!vertex_push(&current->verts, sv)) { ok = 0; break; }
                    face_n++;
                }
                tok = strtok(NULL, " \t\r\n");
            }
            if (!ok) break;
            if (face_n < 3) continue;
            for (int k = 1; k < face_n - 1; k++) {
                if (!index_push(&current->indices, (uint32_t)(face_base + 0)) ||
                    !index_push(&current->indices, (uint32_t)(face_base + k)) ||
                    !index_push(&current->indices, (uint32_t)(face_base + k + 1))) {
                    ok = 0;
                    break;
                }
            }
        }
        /* g, o, s, mtllib, ... ignored (mech supplies the mtl path separately). */
    }

    fclose(f);

    if (!ok) {
        free(positions.data); free(normals.data); free(uvs.data);
        groups_free(&groups);
        return -1;
    }

    /* Lazy-cache: for each mtl_entry, remember the scene material index
     * once it's added. Keeps repeated usemtl from duplicating. */
    int *mtl_scene_idx = NULL;
    if (mtl_count > 0 && mtl_entries) {
        mtl_scene_idx = malloc(sizeof(int) * (size_t)mtl_count);
        if (mtl_scene_idx) {
            for (int i = 0; i < mtl_count; i++) mtl_scene_idx[i] = -1;
        }
    }

    int first_mesh = s->mesh_count;
    int added = 0;

    for (int gi = 0; gi < groups.count; gi++) {
        obj_group *g = &groups.data[gi];
        if (g->indices.count < 3 || g->verts.count < 3) {
            free(g->verts.data);
            free(g->indices.data);
            g->verts.data = NULL;
            g->indices.data = NULL;
            continue;
        }

        int mat_idx = default_material_index;
        if (g->name[0] != '\0' && mtl_entries && mtl_scene_idx) {
            for (int i = 0; i < mtl_count; i++) {
                if (strcmp(mtl_entries[i].name, g->name) == 0) {
                    if (mtl_scene_idx[i] < 0) {
                        mtl_scene_idx[i] = scene_add_material(s, mtl_entries[i].material);
                    }
                    if (mtl_scene_idx[i] >= 0) mat_idx = mtl_scene_idx[i];
                    break;
                }
            }
        }

        fill_face_normals(g->verts.data, g->verts.count,
                          g->indices.data, g->indices.count);

        scene_mesh m = {
            .vertices       = g->verts.data,
            .vertex_count   = g->verts.count,
            .indices        = g->indices.data,
            .index_count    = g->indices.count,
            .material_index = mat_idx,
        };
        scene_mesh_compute_bounds(&m);

        if (scene_add_mesh(s, m) < 0) {
            free(m.vertices);
            free(m.indices);
            /* Stop early on OOM; already-added meshes stay in the scene. */
            break;
        }
        added++;
        /* Ownership transferred — null out so groups_free doesn't double-free. */
        g->verts.data = NULL;
        g->indices.data = NULL;
    }

    free(positions.data); free(normals.data); free(uvs.data);
    groups_free(&groups);
    free(mtl_scene_idx);

    if (first_mesh_index_out) *first_mesh_index_out = first_mesh;
    return added;
}
