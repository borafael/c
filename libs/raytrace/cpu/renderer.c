#include "renderer.h"
#include "render_chunk.h"
#include "thread_pool.h"

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    uint32_t *pixels;
    const rt_viewport *viewport;
    int y_start;
    int y_end;
    const rt_camera *camera;
    const rt_scene *scene;
} cpu_render_task;

static void cpu_render_task_fn(void *arg) {
    cpu_render_task *t = arg;
    rt_render_chunk(t->pixels, t->viewport, t->y_start, t->y_end,
                    t->camera, t->scene);
}

struct rt_renderer {
    thread_pool *pool;
    int num_threads;
    cpu_render_task *tasks;  /* scratch buffer sized [num_threads] */
};

rt_renderer *rt_renderer_create(void) {
    rt_renderer *r = calloc(1, sizeof(*r));
    if (!r) return NULL;

    int n = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 4;

    r->num_threads = n;
    r->pool = thread_pool_create(n);
    if (!r->pool) {
        free(r);
        return NULL;
    }

    r->tasks = malloc(sizeof(cpu_render_task) * (size_t)n);
    if (!r->tasks) {
        thread_pool_destroy(r->pool);
        free(r);
        return NULL;
    }

    return r;
}

void rt_renderer_destroy(rt_renderer *r) {
    if (!r) return;
    thread_pool_destroy(r->pool);
    free(r->tasks);
    free(r);
}

void rt_renderer_render(rt_renderer *r,
                        const rt_scene *scene,
                        const rt_camera *camera,
                        const rt_viewport *viewport,
                        uint32_t *pixels) {
    int rows_per = viewport->height / r->num_threads;
    if (rows_per < 1) rows_per = 1;

    int n = viewport->height / rows_per;
    if (n > r->num_threads) n = r->num_threads;
    if (n < 1) n = 1;

    for (int i = 0; i < n; i++) {
        r->tasks[i] = (cpu_render_task){
            .pixels   = pixels,
            .viewport = viewport,
            .y_start  = i * rows_per,
            .y_end    = (i == n - 1) ? viewport->height : (i + 1) * rows_per,
            .camera   = camera,
            .scene    = scene,
        };
        thread_pool_submit(r->pool, cpu_render_task_fn, &r->tasks[i]);
    }
    thread_pool_wait(r->pool);
}

const char *rt_renderer_name(const rt_renderer *r) {
    (void)r;
    return "CPU";
}
