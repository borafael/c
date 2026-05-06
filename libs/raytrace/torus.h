#ifndef RT_TORUS_H
#define RT_TORUS_H

#include "scene.h"

float  rt_intersect_torus(vector ro, vector rd, const scene_torus *torus);
vector rt_normal_torus(vector hp, const scene_torus *torus);

#endif /* RT_TORUS_H */
