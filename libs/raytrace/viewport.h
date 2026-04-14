#ifndef RT_VIEWPORT_H
#define RT_VIEWPORT_H

/**
 * Viewport defining projection parameters.
 * Shared by all renderer implementations.
 */
typedef struct {
    int width;
    int height;
    float fov;  /* radians */
} rt_viewport;

#endif /* RT_VIEWPORT_H */
