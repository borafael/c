#ifndef RT_RENDERER_H
#define RT_RENDERER_H

#include <stdint.h>
#include "viewport.h"
#include "scene.h"
#include "camera.h"

typedef struct rt_renderer rt_renderer;

/**
 * Available backend implementations. Each value corresponds to a
 * concrete rt_renderer implementation. Which backends are actually
 * built into the library depends on configure-time flags — use
 * rt_renderer_available() to check at runtime.
 */
typedef enum {
    RT_BACKEND_CPU = 0,
} rt_backend;

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
                             const rt_scene *scene,
                             const rt_camera *camera,
                             const rt_viewport *viewport,
                             uint32_t *pixels);
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
 * Create a new renderer. Returns NULL on allocation or thread-pool
 * creation failure. The returned handle must be freed with
 * rt_renderer_destroy.
 *
 * The renderer allocates a thread pool sized to the number of online
 * CPUs. The same pool is reused for every frame — do not create a new
 * renderer per frame.
 */
rt_renderer *rt_renderer_create(void);

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
 * Scene/camera/viewport are passed by pointer and consumed read-only
 * during the call. The caller retains ownership; the renderer does not
 * keep references after the call returns.
 */
void rt_renderer_render(rt_renderer *r,
                        const rt_scene *scene,
                        const rt_camera *camera,
                        const rt_viewport *viewport,
                        uint32_t *pixels);

/**
 * Return a human-readable name for the renderer implementation
 * ("CPU"). The returned string is statically allocated and must not be
 * freed.
 */
const char *rt_renderer_name(const rt_renderer *r);

#endif /* RT_RENDERER_H */
