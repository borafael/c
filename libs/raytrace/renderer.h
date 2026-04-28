#ifndef RT_RENDERER_H
#define RT_RENDERER_H

#include <stdint.h>
#include "viewport.h"
#include "scene.h"

typedef struct rt_renderer rt_renderer;

/**
 * Available backend implementations. Each value corresponds to a
 * concrete rt_renderer implementation. Which backends are actually
 * built into the library depends on configure-time flags — use
 * rt_renderer_available() to check at runtime.
 */
typedef enum {
    RT_BACKEND_CPU = 0,
    RT_BACKEND_OPENGL = 1,
} rt_backend;

/**
 * Per-pixel geometric data captured at the primary ray hit. Pass a
 * non-NULL pointer to rt_renderer_render to fill these alongside the
 * usual colour buffer; pass NULL to skip the work entirely. Useful for
 * post-process effects that need to reason about scene geometry —
 * comic-style outlines, depth-of-field, screen-space AO, etc.
 *
 * Each buffer is width * height entries:
 *   - object_id: 0 means "no hit" (sky). Otherwise an opaque per-primitive
 *     id; equal ids mean the same surface, different ids mean a silhouette
 *     boundary. Stable within a frame, not across frames or scene edits.
 *   - depth: distance from the camera along the primary ray, in world
 *     units. Infinity (or any large value) on miss; check via object_id == 0.
 *   - normal: world-space surface normal, three floats (x,y,z) per pixel
 *     stored interleaved. Zero vector on miss.
 *
 * Only the CPU backend writes the G-buffer today. Asking the OpenGL
 * backend for one currently logs a warning and leaves the buffers as
 * the caller passed them (typically zeroed).
 */
typedef struct {
    uint32_t *object_id;
    float    *depth;
    float    *normal;  /* 3 * width * height — interleaved xyz */
} rt_gbuffer;

/**
 * Renderer vtable. Exposed in the public header so the dispatchers in
 * libs/raytrace/renderer.c can forward through the function pointers
 * without an extra translation-unit hop. Every field is private — do
 * not read or write them directly. Always go through the
 * rt_renderer_* functions below.
 *
 * backend_data points to an allocation owned by the backend (e.g.,
 * cpu_backend_data for the CPU implementation). The backend's
 * destroy_fn is responsible for freeing both backend_data and the
 * rt_renderer itself.
 */
struct rt_renderer {
    void        (*destroy_fn)(struct rt_renderer *r);
    void        (*render_fn)(struct rt_renderer *r,
                             const scene *scene,
                             const scene_camera *camera,
                             const rt_viewport *viewport,
                             uint32_t *pixels,
                             rt_gbuffer *gbuf);
    const char *(*name_fn)(const struct rt_renderer *r);
    void         *backend_data;
};

/**
 * Return non-zero if the given backend was compiled into the library
 * and can be instantiated via rt_renderer_create(). Zero otherwise.
 * This is a pure predicate — it never allocates, never fails, and is
 * safe to call before any other rt_* function.
 */
int rt_renderer_available(rt_backend type);

/**
 * Create a new renderer backed by the requested implementation.
 * Returns NULL if the requested backend is not built into the library
 * (check with rt_renderer_available first) or if allocation fails.
 *
 * The returned handle must be freed with rt_renderer_destroy. Reuse
 * the same handle across frames — do not create a new renderer per
 * frame (backends may do non-trivial setup like allocating a thread
 * pool or compiling GPU kernels).
 */
rt_renderer *rt_renderer_create(rt_backend type);

/**
 * Destroy a renderer and release its resources (thread pool, task
 * scratch buffer). Safe to call with NULL.
 */
void rt_renderer_destroy(rt_renderer *r);

/**
 * Render a single frame into pixels. Fully synchronous: returns only
 * after the frame is complete. pixels must point to at least
 * viewport->width * viewport->height uint32_t's in ARGB8888 format.
 *
 * If gbuf is non-NULL, the backend additionally fills the G-buffer
 * channels (object_id, depth, normal) for every pixel. Pass NULL to
 * skip — most callers want NULL. See the rt_gbuffer doc above.
 *
 * Scene/camera/viewport are passed by pointer and consumed read-only
 * during the call. The caller retains ownership; the renderer does not
 * keep references after the call returns.
 */
void rt_renderer_render(rt_renderer *r,
                        const scene *scene,
                        const scene_camera *camera,
                        const rt_viewport *viewport,
                        uint32_t *pixels,
                        rt_gbuffer *gbuf);

/**
 * Return a human-readable name for the renderer implementation
 * ("CPU"). The returned string is statically allocated and must not be
 * freed.
 */
const char *rt_renderer_name(const rt_renderer *r);

#endif /* RT_RENDERER_H */
