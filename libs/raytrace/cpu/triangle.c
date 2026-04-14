#include "triangle.h"
#include <math.h>

float rt_intersect_triangle(vector ro, vector rd, const rt_triangle *tri) {
    vector e1 = vector_sub(tri->v1, tri->v0);
    vector e2 = vector_sub(tri->v2, tri->v0);
    vector pvec = vector_cross(rd, e2);
    float det = vector_dot(e1, pvec);
    if (fabsf(det) < 1e-6f) return -1.0f;

    float inv_det = 1.0f / det;
    vector tvec = vector_sub(ro, tri->v0);
    float u = vector_dot(tvec, pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) return -1.0f;

    vector qvec = vector_cross(tvec, e1);
    float v = vector_dot(rd, qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;

    float t = vector_dot(e2, qvec) * inv_det;
    if (t < 0.0f) return -1.0f;
    return t;
}

vector rt_normal_triangle(const rt_triangle *tri) {
    vector e1 = vector_sub(tri->v1, tri->v0);
    vector e2 = vector_sub(tri->v2, tri->v0);
    return vector_normalize(vector_cross(e1, e2));
}
