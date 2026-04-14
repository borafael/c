#include "sphere.h"
#include <math.h>

float rt_intersect_sphere(vector ro, vector rd, const rt_sphere *s) {
    vector oc = vector_sub(ro, s->center);
    float b = 2.0f * vector_dot(oc, rd);
    float c = vector_dot(oc, oc) - s->radius * s->radius;
    float disc = b * b - 4.0f * c;
    if (disc < 0.0f) return -1.0f;
    float t = (-b - sqrtf(disc)) * 0.5f;
    if (t < 0.0f) return -1.0f;
    return t;
}

vector rt_normal_sphere(vector hp, const rt_sphere *s) {
    return vector_normalize(vector_sub(hp, s->center));
}
