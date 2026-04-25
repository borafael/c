#include "renderer.h"
#include "render_chunk.h"
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
    /* Per-frame world-transform scratch. Grown on demand. */
    mat4 *node_world;             /* sized scene->node_count */
    int   node_world_capacity;
    mat4 *mesh_world_inv;         /* sized scene->mesh_count */
    int   mesh_world_inv_capacity;
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
    free(d->node_world);
    free(d->mesh_world_inv);
    free(d);
    free(r);
}

/* Compute per-mesh inverse-world matrices from the scene's node tree.
 * Meshes referenced by no node are left at identity (back-compat with
 * OBJ flows that store world-space vertices and add no nodes). Returns
 * NULL if the scene has no meshes or if scratch alloc fails. */
static const mat4 *resolve_mesh_world_inv(cpu_backend_data *d,
                                          const scene *s) {
    if (s->mesh_count <= 0) return NULL;

    if (d->mesh_world_inv_capacity < s->mesh_count) {
        mat4 *grown = realloc(d->mesh_world_inv,
                              sizeof(mat4) * (size_t)s->mesh_count);
        if (!grown) return NULL;
        d->mesh_world_inv = grown;
        d->mesh_world_inv_capacity = s->mesh_count;
    }
    mat4 ident = mat4_identity();
    for (int i = 0; i < s->mesh_count; i++) d->mesh_world_inv[i] = ident;

    if (s->node_count <= 0) return d->mesh_world_inv;

    if (d->node_world_capacity < s->node_count) {
        mat4 *grown = realloc(d->node_world,
                              sizeof(mat4) * (size_t)s->node_count);
        if (!grown) return d->mesh_world_inv;  /* identity is still correct */
        d->node_world = grown;
        d->node_world_capacity = s->node_count;
    }
    scene_resolve_world_transforms(s, d->node_world);

    /* If multiple nodes reference the same mesh, the LAST one wins.
     * In practice each FBX-emitted mesh is owned by exactly one node. */
    for (int i = 0; i < s->node_count; i++) {
        int mi = s->nodes[i].mesh_index;
        if (mi >= 0 && mi < s->mesh_count) {
            d->mesh_world_inv[mi] = mat4_affine_inverse(d->node_world[i]);
        }
    }
    return d->mesh_world_inv;
}

static void cpu_render(rt_renderer *r,
                       const scene *scene,
                       const scene_camera *camera,
                       const rt_viewport *viewport,
                       uint32_t *pixels) {
    cpu_backend_data *d = r->backend_data;

    int rows_per = viewport->height / d->num_threads;
    if (rows_per < 1) rows_per = 1;

    int n = viewport->height / rows_per;
    if (n > d->num_threads) n = d->num_threads;
    if (n < 1) n = 1;

    const mat4 *mesh_world_inv = resolve_mesh_world_inv(d, scene);

    for (int i = 0; i < n; i++) {
        d->tasks[i] = (cpu_render_task){
            .pixels         = pixels,
            .viewport       = viewport,
            .y_start        = i * rows_per,
            .y_end          = (i == n - 1) ? viewport->height : (i + 1) * rows_per,
            .camera         = camera,
            .scene          = scene,
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
