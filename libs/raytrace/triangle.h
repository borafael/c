#ifndef RT_TRIANGLE_H
#define RT_TRIANGLE_H

#include "vector.h"

typedef struct {
    vector v0, v1, v2;
    int material;
} rt_triangle;

float rt_intersect_triangle(vector ro, vector rd, const rt_triangle *tri);
vector rt_normal_triangle(const rt_triangle *tri);

#endif /* RT_TRIANGLE_H */
