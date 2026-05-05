#include "box.h"
#include <math.h>
#include <float.h>

/* Project a world-space point into the box's local frame:
 * lhp = R^T * (p - center), where R = [ux uy uz] is the box's basis.
 * The basis is orthonormal so R^T is just three dot products. */
static inline vector world_to_local(vector p, const scene_box *box) {
    vector d = vector_sub(p, box->center);
    return (vector){
        vector_dot(d, box->ux),
        vector_dot(d, box->uy),
        vector_dot(d, box->uz),
    };
}

float rt_intersect_box(vector ro, vector rd, const scene_box *box) {
    /* Transform the ray into local space and run the standard slab test
     * against (-half_extents, +half_extents). The basis is a rigid
     * rotation, so t is preserved across the change of variables. */
    vector od = vector_sub(ro, box->center);
    float lo[3] = {
        vector_dot(od, box->ux),
        vector_dot(od, box->uy),
        vector_dot(od, box->uz),
    };
    float ld[3] = {
        vector_dot(rd, box->ux),
        vector_dot(rd, box->uy),
        vector_dot(rd, box->uz),
    };
    float h[3] = { box->half_extents.x, box->half_extents.y, box->half_extents.z };

    float tmin = -FLT_MAX, tmax = FLT_MAX;
    for (int i = 0; i < 3; i++) {
        if (fabsf(ld[i]) > 1e-6f) {
            float t0 = (-h[i] - lo[i]) / ld[i];
            float t1 = ( h[i] - lo[i]) / ld[i];
            if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
            if (t0 > tmin) tmin = t0;
            if (t1 < tmax) tmax = t1;
        } else if (lo[i] < -h[i] || lo[i] > h[i]) {
            return -1.0f;
        }
    }
    if (tmin > tmax) return -1.0f;
    if (tmin > 0.0f) return tmin;
    if (tmax > 0.0f) return tmax;
    return -1.0f;
}

vector rt_normal_box(vector hp, const scene_box *box) {
    /* Pick the local face the hit lies on (smallest |slab - |coord||) and
     * lift its sign-aligned local-axis normal back to world via the basis. */
    vector lhp = world_to_local(hp, box);
    float dx = box->half_extents.x - fabsf(lhp.x);
    float dy = box->half_extents.y - fabsf(lhp.y);
    float dz = box->half_extents.z - fabsf(lhp.z);
    if (dx <= dy && dx <= dz) {
        return vector_scale(box->ux, lhp.x >= 0.0f ? 1.0f : -1.0f);
    } else if (dy <= dz) {
        return vector_scale(box->uy, lhp.y >= 0.0f ? 1.0f : -1.0f);
    }
    return vector_scale(box->uz, lhp.z >= 0.0f ? 1.0f : -1.0f);
}

void rt_box_uv(vector hp, const scene_box *box, float *u, float *v) {
    /* Face-aligned planar UVs computed in local space so a rotated box
     * still gets clean per-face texturing. */
    vector lhp = world_to_local(hp, box);
    float dx = box->half_extents.x - fabsf(lhp.x);
    float dy = box->half_extents.y - fabsf(lhp.y);
    float dz = box->half_extents.z - fabsf(lhp.z);
    if (dx <= dy && dx <= dz)      { *u = lhp.z; *v = lhp.y; }
    else if (dy <= dz)             { *u = lhp.x; *v = lhp.z; }
    else                           { *u = lhp.x; *v = lhp.y; }
}
