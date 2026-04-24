#include "box.h"
#include <math.h>
#include <float.h>

float rt_intersect_box(vector ro, vector rd, const scene_box *box) {
    float tmin = -FLT_MAX;
    float tmax = FLT_MAX;

    /* X slab */
    if (fabsf(rd.x) > 1e-6f) {
        float t0 = (box->min.x - ro.x) / rd.x;
        float t1 = (box->max.x - ro.x) / rd.x;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.x < box->min.x || ro.x > box->max.x) {
        return -1.0f;
    }

    /* Y slab */
    if (fabsf(rd.y) > 1e-6f) {
        float t0 = (box->min.y - ro.y) / rd.y;
        float t1 = (box->max.y - ro.y) / rd.y;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.y < box->min.y || ro.y > box->max.y) {
        return -1.0f;
    }

    /* Z slab */
    if (fabsf(rd.z) > 1e-6f) {
        float t0 = (box->min.z - ro.z) / rd.z;
        float t1 = (box->max.z - ro.z) / rd.z;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.z < box->min.z || ro.z > box->max.z) {
        return -1.0f;
    }

    if (tmin > tmax) return -1.0f;
    if (tmin > 0.0f) return tmin;
    if (tmax > 0.0f) return tmax;
    return -1.0f;
}

vector rt_normal_box(vector hp, const scene_box *box) {
    float eps = 1e-4f;
    if (fabsf(hp.x - box->min.x) < eps) return (vector){-1, 0, 0};
    if (fabsf(hp.x - box->max.x) < eps) return (vector){ 1, 0, 0};
    if (fabsf(hp.y - box->min.y) < eps) return (vector){ 0,-1, 0};
    if (fabsf(hp.y - box->max.y) < eps) return (vector){ 0, 1, 0};
    if (fabsf(hp.z - box->min.z) < eps) return (vector){ 0, 0,-1};
    return (vector){0, 0, 1};
}
