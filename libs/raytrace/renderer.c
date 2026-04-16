#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "renderer.h"

#include <stddef.h>

#ifdef RT_HAVE_CPU_BACKEND
/* Forward-declare the CPU backend's internal constructor. This is a
 * cross-translation-unit symbol that lives in libs/raytrace/cpu/renderer.c.
 * It is not part of the public API — callers must go through
 * rt_renderer_create. */
rt_renderer *rt_cpu_renderer_create(void);
#endif

rt_renderer *rt_renderer_create(rt_backend type) {
    switch (type) {
#ifdef RT_HAVE_CPU_BACKEND
    case RT_BACKEND_CPU: return rt_cpu_renderer_create();
#endif
    }
    return NULL;
}

void rt_renderer_destroy(rt_renderer *r) {
    if (!r) return;
    r->destroy_fn(r);
}

void rt_renderer_render(rt_renderer *r,
                        const rt_scene *scene,
                        const rt_camera *camera,
                        const rt_viewport *viewport,
                        uint32_t *pixels) {
    r->render_fn(r, scene, camera, viewport, pixels);
}

const char *rt_renderer_name(const rt_renderer *r) {
    return r->name_fn(r);
}

int rt_renderer_available(rt_backend type) {
    switch (type) {
#ifdef RT_HAVE_CPU_BACKEND
    case RT_BACKEND_CPU: return 1;
#endif
    }
    return 0;
}
