#ifndef RAYTRACE_H
#define RAYTRACE_H

#include <stdint.h>
#include "vector.h"

typedef struct {
    vector center;
    float radius;
    uint8_t r, g, b;
} rt_sphere;

typedef struct {
    vector origin;
    vector forward;
    vector right;
    vector up;
    float fov_factor;
} rt_camera;

typedef struct rt_scene rt_scene;

/**
 * Create a scene that can hold up to max_spheres.
 */
rt_scene *rt_scene_create(int max_spheres);

/**
 * Clear all spheres from the scene.
 */
void rt_scene_clear(rt_scene *scene);

/**
 * Add a sphere to the scene. Returns 0 on success, -1 if full.
 */
int rt_scene_add_sphere(rt_scene *scene, rt_sphere sphere);

/**
 * Destroy the scene and free resources.
 */
void rt_scene_destroy(rt_scene *scene);

/**
 * Render a chunk of scanlines [y_start, y_end) into pixel_buf.
 * pixel_buf is ARGB8888 format, width * height uint32_t's.
 * Caller is responsible for parallelizing across chunks.
 */
void rt_render_chunk(uint32_t *pixel_buf, int width, int height,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene);

#endif /* RAYTRACE_H */
