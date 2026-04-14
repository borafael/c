#ifndef RAYTRACE_H
#define RAYTRACE_H

#include <stdint.h>
#include "vector.h"
#include "rt_color.h"
#include "viewport.h"
#include "sphere.h"
#include "plane.h"
#include "disc.h"
#include "cylinder.h"
#include "triangle.h"
#include "box.h"
#include "sprite.h"
#include "heightfield.h"
#include "scene.h"
#include "camera.h"

/**
 * Render a chunk of scanlines [y_start, y_end) into pixel_buf.
 * pixel_buf is ARGB8888 format, viewport->width * viewport->height uint32_t's.
 * fov is in radians. Caller is responsible for parallelizing across chunks.
 */
void rt_render_chunk(uint32_t *pixel_buf, const rt_viewport *viewport,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene);

#endif /* RAYTRACE_H */
