#ifndef RT_SPRITE_H
#define RT_SPRITE_H

#include "scene.h"
#include <stdint.h>

float    rt_intersect_sprite(vector ro, vector rd, const scene_sprite *spr,
                             vector cam_origin, vector *out_right,
                             vector *out_up, vector *out_normal);
int      rt_sprite_select_frame(const scene_sprite *spr, vector cam_origin);
uint32_t rt_sprite_sample(const scene_sprite *spr, const scene_frame *frame,
                          vector hp, vector right, vector up);
float    rt_pick_sprite(vector ray_origin, vector ray_dir,
                        const scene_sprite *sprite, vector camera_origin,
                        vector *hit_point);

#endif /* RT_SPRITE_H */
