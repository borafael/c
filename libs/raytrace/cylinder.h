#ifndef RT_CYLINDER_H
#define RT_CYLINDER_H

#include "scene.h"

float  rt_intersect_cylinder(vector ro, vector rd, const scene_cylinder *cyl);
vector rt_normal_cylinder(vector hp, const scene_cylinder *cyl);

#endif /* RT_CYLINDER_H */
