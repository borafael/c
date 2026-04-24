#include "heightfield.h"
#include <math.h>
#include <float.h>

static int hf_aabb_test(const scene_heightfield *hf, vector ro, vector rd,
                         float *t_enter, float *t_exit) {
    float xmin = hf->origin_x;
    float xmax = hf->origin_x + hf->world_width;
    float ymin = 0.0f;
    float ymax = hf->max_height;
    float zmin = hf->origin_z;
    float zmax = hf->origin_z + hf->world_depth;

    float tmin = -FLT_MAX, tmax = FLT_MAX;

    if (fabsf(rd.x) > 1e-6f) {
        float t0 = (xmin - ro.x) / rd.x;
        float t1 = (xmax - ro.x) / rd.x;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.x < xmin || ro.x > xmax) return 0;

    if (fabsf(rd.y) > 1e-6f) {
        float t0 = (ymin - ro.y) / rd.y;
        float t1 = (ymax - ro.y) / rd.y;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.y < ymin || ro.y > ymax) return 0;

    if (fabsf(rd.z) > 1e-6f) {
        float t0 = (zmin - ro.z) / rd.z;
        float t1 = (zmax - ro.z) / rd.z;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.z < zmin || ro.z > zmax) return 0;

    if (tmin > tmax) return 0;
    *t_enter = tmin > 0.0f ? tmin : 0.0f;
    *t_exit = tmax;
    if (tmax < 0.0f) return 0;
    return 1;
}

static float hf_intersect_tri(vector ro, vector rd,
                                vector v0, vector v1, vector v2,
                                float *out_u, float *out_v) {
    vector e1 = vector_sub(v1, v0);
    vector e2 = vector_sub(v2, v0);
    vector pvec = vector_cross(rd, e2);
    float det = vector_dot(e1, pvec);
    if (fabsf(det) < 1e-6f) return -1.0f;

    float inv_det = 1.0f / det;
    vector tvec = vector_sub(ro, v0);
    float u = vector_dot(tvec, pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) return -1.0f;

    vector qvec = vector_cross(tvec, e1);
    float v = vector_dot(rd, qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;

    float t = vector_dot(e2, qvec) * inv_det;
    if (t < 0.0f) return -1.0f;

    *out_u = u;
    *out_v = v;
    return t;
}

static vector hf_vertex_pos(const scene_heightfield *hf, int r, int c) {
    float cell_w = hf->world_width / (float)(hf->cols - 1);
    float cell_d = hf->world_depth / (float)(hf->rows - 1);
    return (vector){
        hf->origin_x + c * cell_w,
        hf->heights[r * hf->cols + c],
        hf->origin_z + r * cell_d
    };
}

static vector hf_vertex_normal(const scene_heightfield *hf, int r, int c) {
    int idx = (r * hf->cols + c) * 3;
    return (vector){ hf->normals[idx], hf->normals[idx + 1], hf->normals[idx + 2] };
}

int rt_intersect_heightfield(const scene_heightfield *hf, vector origin, vector dir,
                              float *out_t, vector *out_normal,
                              int *out_cell_r, int *out_cell_c) {
    float t_enter, t_exit;
    if (!hf_aabb_test(hf, origin, dir, &t_enter, &t_exit)) return 0;

    int cells_x = hf->cols - 1;
    int cells_z = hf->rows - 1;
    float cell_w = hf->world_width / (float)cells_x;
    float cell_d = hf->world_depth / (float)cells_z;

    float eps = 1e-4f;
    vector entry = vector_add(origin, vector_scale(dir, t_enter + eps));

    float gx = (entry.x - hf->origin_x) / cell_w;
    float gz = (entry.z - hf->origin_z) / cell_d;

    int cx = (int)floorf(gx);
    int cz = (int)floorf(gz);

    if (cx < 0) cx = 0; if (cx >= cells_x) cx = cells_x - 1;
    if (cz < 0) cz = 0; if (cz >= cells_z) cz = cells_z - 1;

    int step_x = (dir.x >= 0.0f) ? 1 : -1;
    int step_z = (dir.z >= 0.0f) ? 1 : -1;

    float t_delta_x = (fabsf(dir.x) > 1e-6f) ? fabsf(cell_w / dir.x) : FLT_MAX;
    float t_delta_z = (fabsf(dir.z) > 1e-6f) ? fabsf(cell_d / dir.z) : FLT_MAX;

    float next_x_boundary = hf->origin_x + ((dir.x >= 0.0f) ? (cx + 1) : cx) * cell_w;
    float next_z_boundary = hf->origin_z + ((dir.z >= 0.0f) ? (cz + 1) : cz) * cell_d;

    float t_max_x = (fabsf(dir.x) > 1e-6f)
        ? (next_x_boundary - origin.x) / dir.x : FLT_MAX;
    float t_max_z = (fabsf(dir.z) > 1e-6f)
        ? (next_z_boundary - origin.z) / dir.z : FLT_MAX;

    float best_t = FLT_MAX;
    vector best_normal = {0, 1, 0};
    int best_cr = -1, best_cc = -1;

    int max_steps = cells_x + cells_z + 2;
    for (int step = 0; step < max_steps; step++) {
        if (cx < 0 || cx >= cells_x || cz < 0 || cz >= cells_z) break;

        vector v00 = hf_vertex_pos(hf, cz,     cx);
        vector v10 = hf_vertex_pos(hf, cz + 1, cx);
        vector v01 = hf_vertex_pos(hf, cz,     cx + 1);
        vector v11 = hf_vertex_pos(hf, cz + 1, cx + 1);

        vector n00 = hf_vertex_normal(hf, cz,     cx);
        vector n10 = hf_vertex_normal(hf, cz + 1, cx);
        vector n01 = hf_vertex_normal(hf, cz,     cx + 1);
        vector n11 = hf_vertex_normal(hf, cz + 1, cx + 1);

        float u, v;
        float t = hf_intersect_tri(origin, dir, v00, v10, v01, &u, &v);
        if (t > 0.0f && t < best_t) {
            best_t = t;
            float w = 1.0f - u - v;
            best_normal = vector_normalize((vector){
                w * n00.x + u * n10.x + v * n01.x,
                w * n00.y + u * n10.y + v * n01.y,
                w * n00.z + u * n10.z + v * n01.z
            });
            best_cr = cz;
            best_cc = cx;
        }

        t = hf_intersect_tri(origin, dir, v10, v11, v01, &u, &v);
        if (t > 0.0f && t < best_t) {
            best_t = t;
            float w = 1.0f - u - v;
            best_normal = vector_normalize((vector){
                w * n10.x + u * n11.x + v * n01.x,
                w * n10.y + u * n11.y + v * n01.y,
                w * n10.z + u * n11.z + v * n01.z
            });
            best_cr = cz;
            best_cc = cx;
        }

        if (best_t < FLT_MAX && best_t <= fminf(t_max_x, t_max_z) + eps) break;

        if (t_max_x < t_max_z) {
            cx += step_x;
            t_max_x += t_delta_x;
        } else {
            cz += step_z;
            t_max_z += t_delta_z;
        }
    }

    if (best_t < FLT_MAX) {
        *out_t = best_t;
        *out_normal = best_normal;
        if (out_cell_r) *out_cell_r = best_cr;
        if (out_cell_c) *out_cell_c = best_cc;
        return 1;
    }
    return 0;
}
