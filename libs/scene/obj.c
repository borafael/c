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
