#ifndef RT_TRIANGLE_H
#define RT_TRIANGLE_H

#include "vector.h"
#include "rt_color.h"

typedef struct {
    vector v0, v1, v2;
    rt_color color;
} rt_triangle;

float rt_intersect_triangle(vector ro, vector rd, const rt_triangle *tri);
vector rt_normal_triangle(const rt_triangle *tri);

#endif /* RT_TRIANGLE_H */
