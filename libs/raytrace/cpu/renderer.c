#include "renderer.h"
#include "render_chunk.h"
#include "scene_accel.h"
#include "thread_pool.h"
#include "matrix.h"

#include <stdint.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static int detect_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

typedef struct {
    uint32_t *pixels;
    const rt_viewport *viewport;
    int y_start;
    int y_end;
    const scene_camera *camera;
    const scene *scene;
    const mat4 *mesh_world_inv;
} cpu_render_task;

typedef struct {
    thread_pool *pool;
    int num_threads;
    cpu_render_task *tasks;       /* scratch buffer sized [num_threads] */
    rt_scene_accel accel;         /* per-frame node/mesh transform scratch */
} cpu_backend_data;

static void cpu_render_task_fn(void *arg) {
    cpu_render_task *t = arg;
    rt_render_chunk(t->pixels, t->viewport, t->y_start, t->y_end,
                    t->camera, t->scene, t->mesh_world_inv);
}

static void cpu_destroy(rt_renderer *r) {
    cpu_backend_data *d = r->backend_data;
    thread_pool_destroy(d->pool);
    free(d->tasks);
    rt_scene_accel_dispose(&d->accel);
    free(d);
    free(r);
}

static void cpu_render(rt_renderer *r,
                       const scene *scene_in,
                       const scene_camera *camera,
                       const rt_viewport *viewport,
                       uint32_t *pixels) {
    cpu_backend_data *d = r->backend_data;
    /* Skinning mutates mesh vertex buffers and BVHs; the rest of the
     * render path treats the scene as read-only. The renderer interface
     * passes a const pointer for the rigid contract — cast away locally. */
    scene *scn = (scene *)(uintptr_t)scene_in;

    int rows_per = viewport->height / d->num_threads;
    if (rows_per < 1) rows_per = 1;

    int n = viewport->height / rows_per;
    if (n > d->num_threads) n = d->num_threads;
    if (n < 1) n = 1;

    const mat4 *mesh_world_inv = NULL;
    if (rt_scene_accel_resolve(&d->accel, scn) && scn->mesh_count > 0) {
        mesh_world_inv = d->accel.mesh_world_inv;
    }

    for (int i = 0; i < n; i++) {
        d->tasks[i] = (cpu_render_task){
            .pixels         = pixels,
            .viewport       = viewport,
            .y_start        = i * rows_per,
            .y_end          = (i == n - 1) ? viewport->height : (i + 1) * rows_per,
            .camera         = camera,
            .scene          = scn,
            .mesh_world_inv = mesh_world_inv,
        };
        thread_pool_submit(d->pool, cpu_render_task_fn, &d->tasks[i]);
    }
    thread_pool_wait(d->pool);
}

static const char *cpu_name(const rt_renderer *r) {
    (void)r;
    return "CPU";
}

rt_renderer *rt_cpu_renderer_create(void) {
    rt_renderer *r = calloc(1, sizeof(*r));
    if (!r) return NULL;

    cpu_backend_data *d = calloc(1, sizeof(*d));
    if (!d) {
        free(r);
        return NULL;
    }

    int n = detect_cpu_count();
    if (n < 1) n = 4;

    d->num_threads = n;
    d->pool = thread_pool_create(n);
    if (!d->pool) {
        free(d);
        free(r);
        return NULL;
    }

    d->tasks = malloc(sizeof(cpu_render_task) * (size_t)n);
    if (!d->tasks) {
        thread_pool_destroy(d->pool);
        free(d);
        free(r);
        return NULL;
    }

    r->destroy_fn   = cpu_destroy;
    r->render_fn    = cpu_render;
    r->name_fn      = cpu_name;
    r->backend_data = d;
    return r;
}
