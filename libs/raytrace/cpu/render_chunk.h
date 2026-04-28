#ifndef RT_CPU_RENDER_CHUNK_H
#define RT_CPU_RENDER_CHUNK_H

#include <stdint.h>
#include "viewport.h"
#include "scene.h"
#include "matrix.h"
#include "renderer.h"   /* rt_gbuffer */

/**
 * Render a chunk of scanlines [y_start, y_end) into pixel_buf.
 * pixel_buf is ARGB8888 format, viewport->width * viewport->height uint32_t's.
 * fov is in radians. Caller is responsible for parallelizing across chunks.
 *
 * If gbuf is non-NULL, the chunk also writes per-pixel object_id, depth
 * and normal at the primary hit. Each chunk only touches its own row
 * range, so concurrent calls with the same gbuf are safe.
 *
 * `mesh_world_inv` is parallel to scene->meshes (one mat4 per mesh) and
 * holds the inverse of each mesh's resolved world transform. Pass NULL
 * to treat all meshes as living in their stored vertex space (legacy
 * OBJ-loader behavior). The renderer transforms the ray into mesh-local
 * space before BVH traversal.
 *
 * CPU backend internal — only cpu/renderer.c calls this function. Not
 * part of the public rt_renderer API.
 */
void rt_render_chunk(uint32_t *pixel_buf, rt_gbuffer *gbuf,
                     const rt_viewport *viewport,
                     int y_start, int y_end,
                     const scene_camera *camera, const scene *scene,
                     const mat4 *mesh_world_inv);

#endif /* RT_CPU_RENDER_CHUNK_H */
