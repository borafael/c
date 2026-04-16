#include "renderer.h"

#include <stddef.h>

/* Forward-declare the CPU backend's internal constructor. This is a
 * cross-translation-unit symbol that lives in libs/raytrace/cpu/renderer.c.
 * It is not part of the public API — callers must go through
 * rt_renderer_create. */
rt_renderer *rt_cpu_renderer_create(void);

rt_renderer *rt_renderer_create(void) {
    return rt_cpu_renderer_create();
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
    case RT_BACKEND_CPU: return 1;
    }
    return 0;
}
