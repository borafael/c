#ifndef RT_CONE_H
#define RT_CONE_H

#include "scene.h"

float  rt_intersect_cone(vector ro, vector rd, const scene_cone *cone);
vector rt_normal_cone(vector hp, const scene_cone *cone);

#endif /* RT_CONE_H */
