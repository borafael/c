#ifndef RT_TRIANGLE_H
#define RT_TRIANGLE_H

#include "scene.h"

float  rt_intersect_triangle(vector ro, vector rd, const scene_triangle *tri);
vector rt_normal_triangle(const scene_triangle *tri);

#endif /* RT_TRIANGLE_H */
