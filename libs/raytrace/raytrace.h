#ifndef RAYTRACE_H
#define RAYTRACE_H

#include <stdint.h>
#include "vector.h"

typedef struct {
    uint8_t r, g, b;
} rt_color;

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

typedef struct {
    vector center;
    float radius;
    rt_color color;
} rt_sphere;

typedef struct {
    vector normal;
    vector point;
    rt_color color;
} rt_plane;

typedef struct {
    vector center;
    vector normal;
    float radius;
    rt_color color;
} rt_disc;

typedef struct {
    vector center;
    vector axis;
    float radius;
    float half_height;
    rt_color color;
} rt_cylinder;

typedef struct {
    vector v0, v1, v2;
    rt_color color;
} rt_triangle;

typedef struct {
    vector min;
    vector max;
    rt_color color;
} rt_box;

typedef struct {
    vector direction;
    float intensity;
} rt_light;

typedef struct rt_camera rt_camera;

/**
 * Create a camera at position, looking toward direction.
 * Computes internal orientation vectors automatically.
 */
rt_camera *rt_camera_create(vector position, vector direction);

/**
 * Reposition the camera and change its direction.
 */
void rt_camera_place(rt_camera *cam, vector position, vector direction);

/**
 * Destroy the camera and free resources.
 */
void rt_camera_destroy(rt_camera *cam);

typedef struct rt_scene rt_scene;

/**
 * Create an empty scene.
 */
rt_scene *rt_scene_create(void);

/**
 * Clear all shapes from the scene.
 */
void rt_scene_clear(rt_scene *scene);

void rt_scene_set_ambient(rt_scene *scene, float ambient);
int rt_scene_add_light(rt_scene *scene, rt_light light);
int rt_scene_add_sphere(rt_scene *scene, rt_sphere sphere);
int rt_scene_add_plane(rt_scene *scene, rt_plane plane);
int rt_scene_add_disc(rt_scene *scene, rt_disc disc);
int rt_scene_add_cylinder(rt_scene *scene, rt_cylinder cylinder);
int rt_scene_add_triangle(rt_scene *scene, rt_triangle triangle);
int rt_scene_add_box(rt_scene *scene, rt_box box);
int rt_scene_add_sprite(rt_scene *scene, rt_sprite sprite);

/**
 * Destroy the scene and free resources.
 */
void rt_scene_destroy(rt_scene *scene);

/**
 * Viewport defining projection parameters.
 */
typedef struct {
    int width;
    int height;
    float fov;
} rt_viewport;

/**
 * Render a chunk of scanlines [y_start, y_end) into pixel_buf.
 * pixel_buf is ARGB8888 format, viewport->width * viewport->height uint32_t's.
 * fov is in radians. Caller is responsible for parallelizing across chunks.
 */
void rt_render_chunk(uint32_t *pixel_buf, const rt_viewport *viewport,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene);

#endif /* RAYTRACE_H */
