#ifndef RT_CPU_RENDER_CHUNK_H
#define RT_CPU_RENDER_CHUNK_H

#include <stdint.h>
#include "viewport.h"
#include "camera.h"
#include "scene.h"

/**
 * Render a chunk of scanlines [y_start, y_end) into pixel_buf.
 * pixel_buf is ARGB8888 format, viewport->width * viewport->height uint32_t's.
 * fov is in radians. Caller is responsible for parallelizing across chunks.
 *
 * CPU backend internal — only cpu/renderer.c calls this function. Not
 * part of the public rt_renderer API.
 */
void rt_render_chunk(uint32_t *pixel_buf, const rt_viewport *viewport,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene);

#endif /* RT_CPU_RENDER_CHUNK_H */
