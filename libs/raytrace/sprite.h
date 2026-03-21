#ifndef RT_SPRITE_H
#define RT_SPRITE_H

#include "vector.h"
#include <stdint.h>

typedef struct {
    uint32_t *pixels;   /* ARGB8888 pixel data (not owned by raytracer) */
    int width;
    int height;
} rt_frame;

typedef struct {
    vector position;     /* center in world space */
    vector direction;    /* facing direction (for angle selection only) */
    float width;         /* world-space quad width */
    float height;        /* world-space quad height */
    int frame_count;     /* number of viewing angles */
    rt_frame *frames;    /* one frame per angle, clockwise from front */
} rt_sprite;

float rt_intersect_sprite(vector ro, vector rd, const rt_sprite *spr,
                           vector cam_origin, vector *out_right,
                           vector *out_up, vector *out_normal);
int rt_sprite_select_frame(const rt_sprite *spr, vector cam_origin);
uint32_t rt_sprite_sample(const rt_sprite *spr, const rt_frame *frame,
                            vector hp, vector right, vector up);
float rt_pick_sprite(vector ray_origin, vector ray_dir,
                     const rt_sprite *sprite, vector camera_origin,
                     vector *hit_point);

#endif /* RT_SPRITE_H */
