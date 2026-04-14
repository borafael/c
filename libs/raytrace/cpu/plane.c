#include "plane.h"
#include <math.h>

float rt_intersect_plane(vector ro, vector rd, const rt_plane *p) {
    float denom = vector_dot(rd, p->normal);
    if (fabsf(denom) < 1e-6f) return -1.0f;
    float t = vector_dot(vector_sub(p->point, ro), p->normal) / denom;
    if (t < 0.0f) return -1.0f;
    return t;
}

vector rt_normal_plane(const rt_plane *p) {
    return p->normal;
}
