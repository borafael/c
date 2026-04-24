#ifndef RT_DISC_H
#define RT_DISC_H

#include "scene.h"

float  rt_intersect_disc(vector ro, vector rd, const scene_disc *d);
vector rt_normal_disc(const scene_disc *d);

#endif /* RT_DISC_H */
