#include "cylinder.h"
#include <math.h>

float rt_intersect_cylinder(vector ro, vector rd, const scene_cylinder *cyl) {
    vector oc = vector_sub(ro, cyl->center);
    float rd_dot_a = vector_dot(rd, cyl->axis);
    float oc_dot_a = vector_dot(oc, cyl->axis);

    vector rd_perp = vector_sub(rd, vector_scale(cyl->axis, rd_dot_a));
    vector oc_perp = vector_sub(oc, vector_scale(cyl->axis, oc_dot_a));

    float a = vector_dot(rd_perp, rd_perp);
    float b = 2.0f * vector_dot(oc_perp, rd_perp);
    float c = vector_dot(oc_perp, oc_perp) - cyl->radius * cyl->radius;

    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return -1.0f;

    float sqrt_disc = sqrtf(disc);
    float t0 = (-b - sqrt_disc) / (2.0f * a);
    float t1 = (-b + sqrt_disc) / (2.0f * a);

    for (int i = 0; i < 2; i++) {
        float t = (i == 0) ? t0 : t1;
        if (t < 0.0f) continue;
        float h = oc_dot_a + t * rd_dot_a;
        if (h >= -cyl->half_height && h <= cyl->half_height) return t;
    }
    return -1.0f;
}

vector rt_normal_cylinder(vector hp, const scene_cylinder *cyl) {
    vector diff = vector_sub(hp, cyl->center);
    float proj = vector_dot(diff, cyl->axis);
    vector on_axis = vector_add(cyl->center, vector_scale(cyl->axis, proj));
    return vector_normalize(vector_sub(hp, on_axis));
}
