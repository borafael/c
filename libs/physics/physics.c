#include "physics.h"
#include "thread_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int i;
    int j;
} merge_pair;

typedef struct {
    physics_world *world;
    int            start;
    int            end;
    vector        *local_accel;  /* sized to world->capacity */
    merge_pair    *local_merges; /* sized to per-thread merge cap */
    int            merge_cap;
    int            merge_count;
} force_task;

struct physics_world {
    physics_config config;

    thread_pool *pool;

    int     capacity;
    int     body_count;

    /* Per-body arrays, length == capacity. */
    int    *alive;
    vector *position;
    vector *velocity;
    vector *acceleration;
    float  *mass;

    /* Free-slot stack. */
    int *free_stack;
    int  free_top; /* -1 when empty; points at last pushed */

    /* Merge bookkeeping. */
    merge_pair *merge_list;
    int         merge_cap;
    int         merge_count;

    /* Per-thread scratch. */
    force_task *tasks;
};

static int index_in_range(const physics_world *w, int id) {
    return id >= 0 && id < w->capacity;
}

physics_config physics_default_config(void) {
    physics_config c;
    c.max_bodies       = 300;
    c.num_threads      = 4;
    c.gravity          = 0.5f;
    c.dt               = 0.016f;
    c.softening        = 5.0f;
    c.merge_on_contact = 1;
    c.bounded          = 0;
    c.world_radius     = 400.0f;
    return c;
}

physics_world *physics_world_create(const physics_config *config) {
    if (!config) return NULL;

    int cap = config->max_bodies;
    int nt  = config->num_threads;
    if (cap < 1) cap = 1;
    if (nt  < 1) nt  = 1;

    physics_world *w = calloc(1, sizeof(*w));
    if (!w) return NULL;

    w->config             = *config;
    w->config.max_bodies  = cap;
    w->config.num_threads = nt;
    w->capacity           = cap;
    w->body_count         = 0;
    w->free_top           = -1;
    w->merge_cap          = cap / 2 + 1;
    w->merge_count        = 0;

    w->alive        = calloc((size_t)cap, sizeof(*w->alive));
    w->position     = calloc((size_t)cap, sizeof(*w->position));
    w->velocity     = calloc((size_t)cap, sizeof(*w->velocity));
    w->acceleration = calloc((size_t)cap, sizeof(*w->acceleration));
    w->mass         = calloc((size_t)cap, sizeof(*w->mass));
    w->free_stack   = calloc((size_t)cap, sizeof(*w->free_stack));
    w->merge_list   = calloc((size_t)w->merge_cap, sizeof(*w->merge_list));
    w->tasks        = calloc((size_t)nt, sizeof(*w->tasks));

    if (!w->alive || !w->position || !w->velocity || !w->acceleration
        || !w->mass || !w->free_stack || !w->merge_list || !w->tasks) {
        physics_world_destroy(w);
        return NULL;
    }

    /* Seed free stack with ids in reverse so IDs are handed out 0..n. */
    for (int i = cap - 1; i >= 0; i--) {
        w->free_stack[++w->free_top] = i;
    }

    int per_thread_merge_cap = w->merge_cap / nt;
    if (per_thread_merge_cap < 1) per_thread_merge_cap = 1;

    for (int t = 0; t < nt; t++) {
        w->tasks[t].world        = w;
        w->tasks[t].local_accel  = calloc((size_t)cap, sizeof(vector));
        w->tasks[t].local_merges = calloc((size_t)per_thread_merge_cap,
                                          sizeof(merge_pair));
        w->tasks[t].merge_cap    = per_thread_merge_cap;
        if (!w->tasks[t].local_accel || !w->tasks[t].local_merges) {
            physics_world_destroy(w);
            return NULL;
        }
    }

    w->pool = thread_pool_create(nt);
    if (!w->pool) {
        physics_world_destroy(w);
        return NULL;
    }

    return w;
}

void physics_world_destroy(physics_world *w) {
    if (!w) return;
    if (w->pool) thread_pool_destroy(w->pool);
    if (w->tasks) {
        for (int t = 0; t < w->config.num_threads; t++) {
            free(w->tasks[t].local_accel);
            free(w->tasks[t].local_merges);
        }
        free(w->tasks);
    }
    free(w->alive);
    free(w->position);
    free(w->velocity);
    free(w->acceleration);
    free(w->mass);
    free(w->free_stack);
    free(w->merge_list);
    free(w);
}

int physics_world_add_body(physics_world *w, vector position, vector velocity,
                           float mass) {
    if (!w || w->free_top < 0) return -1;
    int id = w->free_stack[w->free_top--];
    w->alive[id]        = 1;
    w->position[id]     = position;
    w->velocity[id]     = velocity;
    w->acceleration[id] = (vector){0, 0, 0};
    w->mass[id]         = mass;
    w->body_count++;
    return id;
}

void physics_world_remove_body(physics_world *w, int id) {
    if (!w || !index_in_range(w, id) || !w->alive[id]) return;
    w->alive[id] = 0;
    w->free_stack[++w->free_top] = id;
    w->body_count--;
}

void physics_world_clear(physics_world *w) {
    if (!w) return;
    for (int i = 0; i < w->capacity; i++) w->alive[i] = 0;
    w->free_top = -1;
    for (int i = w->capacity - 1; i >= 0; i--) {
        w->free_stack[++w->free_top] = i;
    }
    w->body_count = 0;
}

int physics_world_capacity(const physics_world *w) {
    return w ? w->capacity : 0;
}

int physics_world_body_count(const physics_world *w) {
    return w ? w->body_count : 0;
}

int physics_world_body_alive(const physics_world *w, int id) {
    if (!w || !index_in_range(w, id)) return 0;
    return w->alive[id];
}

vector physics_world_body_position(const physics_world *w, int id) {
    if (!w || !index_in_range(w, id)) return (vector){0, 0, 0};
    return w->position[id];
}

vector physics_world_body_velocity(const physics_world *w, int id) {
    if (!w || !index_in_range(w, id)) return (vector){0, 0, 0};
    return w->velocity[id];
}

float physics_world_body_mass(const physics_world *w, int id) {
    if (!w || !index_in_range(w, id)) return 0.0f;
    return w->mass[id];
}

/* ------------------------------------------------------------------ */
/* Simulation step                                                     */
/* ------------------------------------------------------------------ */

static void compute_forces_chunk(void *arg) {
    force_task *t = arg;
    physics_world *w = t->world;

    memset(t->local_accel, 0, (size_t)w->capacity * sizeof(vector));
    t->merge_count = 0;

    const float G   = w->config.gravity;
    const float eps = w->config.softening;

    for (int i = t->start; i < t->end; i++) {
        if (!w->alive[i]) continue;

        for (int j = i + 1; j < w->capacity; j++) {
            if (!w->alive[j]) continue;

            vector diff = vector_sub(w->position[j], w->position[i]);
            float  dist = vector_magnitude(diff);

            if (dist < eps) {
                if (w->config.merge_on_contact && t->merge_count < t->merge_cap) {
                    t->local_merges[t->merge_count].i = i;
                    t->local_merges[t->merge_count].j = j;
                    t->merge_count++;
                }
                continue;
            }

            float  force    = G * w->mass[i] * w->mass[j] / (dist * dist);
            vector dir      = vector_scale(diff, 1.0f / dist);
            vector force_v  = vector_scale(dir, force);

            t->local_accel[i] = vector_add(t->local_accel[i],
                vector_scale(force_v, 1.0f / w->mass[i]));
            t->local_accel[j] = vector_sub(t->local_accel[j],
                vector_scale(force_v, 1.0f / w->mass[j]));
        }
    }
}

static void reset_accelerations(physics_world *w) {
    for (int i = 0; i < w->capacity; i++) {
        if (w->alive[i]) w->acceleration[i] = (vector){0, 0, 0};
    }
}

static void accumulate_forces(physics_world *w) {
    int nt    = w->config.num_threads;
    int chunk = w->capacity / nt;
    if (chunk < 1) chunk = 1;

    for (int t = 0; t < nt; t++) {
        w->tasks[t].start = t * chunk;
        w->tasks[t].end   = (t == nt - 1) ? w->capacity : (t + 1) * chunk;
        thread_pool_submit(w->pool, compute_forces_chunk, &w->tasks[t]);
    }
    thread_pool_wait(w->pool);

    for (int t = 0; t < nt; t++) {
        for (int i = 0; i < w->capacity; i++) {
            w->acceleration[i] = vector_add(w->acceleration[i],
                                            w->tasks[t].local_accel[i]);
        }
    }

    w->merge_count = 0;
    for (int t = 0; t < nt; t++) {
        for (int m = 0; m < w->tasks[t].merge_count; m++) {
            if (w->merge_count < w->merge_cap) {
                w->merge_list[w->merge_count++] = w->tasks[t].local_merges[m];
            }
        }
    }
}

static void apply_merges(physics_world *w) {
    for (int m = 0; m < w->merge_count; m++) {
        int i = w->merge_list[m].i;
        int j = w->merge_list[m].j;
        if (!w->alive[i] || !w->alive[j]) continue;

        float mi = w->mass[i];
        float mj = w->mass[j];
        float mt = mi + mj;

        w->position[i] = vector_scale(
            vector_add(vector_scale(w->position[i], mi),
                       vector_scale(w->position[j], mj)),
            1.0f / mt);

        w->velocity[i] = vector_scale(
            vector_add(vector_scale(w->velocity[i], mi),
                       vector_scale(w->velocity[j], mj)),
            1.0f / mt);

        w->mass[i] = mt;
        physics_world_remove_body(w, j);
    }
}

static void integrate_body(physics_world *w, int id) {
    w->velocity[id] = vector_add(w->velocity[id],
                                 vector_scale(w->acceleration[id], w->config.dt));
    w->position[id] = vector_add(w->position[id],
                                 vector_scale(w->velocity[id], w->config.dt));
}

/* Reflective sphere: if a body is outside the boundary and moving
 * outward, flip the outward component of its velocity and lose half
 * its kinetic energy (matches the original nbody behavior). */
static void reflect_body(physics_world *w, int id) {
    float dist = vector_magnitude(w->position[id]);
    if (dist <= w->config.world_radius) return;

    vector normal = vector_scale(w->position[id], 1.0f / dist);
    w->position[id] = vector_scale(normal, w->config.world_radius);

    float vn = vector_dot(w->velocity[id], normal);
    if (vn > 0.0f) {
        w->velocity[id] = vector_sub(w->velocity[id],
                                     vector_scale(normal, 2.0f * vn));
        w->velocity[id] = vector_scale(w->velocity[id], 0.5f);
    }
}

void physics_world_step(physics_world *w) {
    if (!w) return;
    reset_accelerations(w);
    accumulate_forces(w);
    apply_merges(w);

    for (int i = 0; i < w->capacity; i++) {
        if (!w->alive[i]) continue;
        integrate_body(w, i);
        if (w->config.bounded) reflect_body(w, i);
    }
}
