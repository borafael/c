#ifndef RT_TEXTURE_H
#define RT_TEXTURE_H

#include <stdint.h>

typedef struct {
    uint32_t *pixels;   /* ARGB8888, not owned by raytracer */
    int width;
    int height;
} rt_texture;

#endif /* RT_TEXTURE_H */
