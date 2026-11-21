#include "physics.h"
#include "thread_pool.h"

#include <math.h>
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
    float  *radius;
    float  *restitution;
    float  *age;
    unsigned char *type;      /* physics_body_type */
    unsigned char *disabled;
    unsigned char *sleeping;

    physics_column *columns;  /* copy of config->columns */

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

/* Alive, enabled and of the given type. */
static int active(const physics_world *w, int id, physics_body_type t) {
    return w->alive[id] && !w->disabled[id] && w->type[id] == t;
}

static int has_mutual_gravity(const physics_world *w) {
    return w->config.gravity != 0.0f || w->config.merge_on_contact;
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

    c.fall_gravity        = 0.0f;
    c.collisions          = 0;
    c.ground              = 0;
    c.ground_y            = 0.0f;
    c.ground_restitution  = 0.5f;
    c.ground_grip         = 1.0f;
    c.ground_settle_speed = 0.0f;
    c.roll_friction       = 0.0f;
    c.sleep_speed         = 0.0f;
    c.walls               = 0;
    c.wall_min_x = c.wall_min_z = -1.0f;
    c.wall_max_x = c.wall_max_z =  1.0f;
    c.wall_restitution    = 0.5f;
    c.columns             = NULL;
    c.column_count        = 0;
    c.max_substep_dist    = 0.0f;
    c.min_substeps        = 1;
    c.max_substeps        = 1;
    c.on_contact          = NULL;
    c.contact_user        = NULL;
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
    w->radius       = calloc((size_t)cap, sizeof(*w->radius));
    w->restitution  = calloc((size_t)cap, sizeof(*w->restitution));
    w->age          = calloc((size_t)cap, sizeof(*w->age));
    w->type         = calloc((size_t)cap, sizeof(*w->type));
    w->disabled     = calloc((size_t)cap, sizeof(*w->disabled));
    w->sleeping     = calloc((size_t)cap, sizeof(*w->sleeping));
    w->free_stack   = calloc((size_t)cap, sizeof(*w->free_stack));
    w->merge_list   = calloc((size_t)w->merge_cap, sizeof(*w->merge_list));
    w->tasks        = calloc((size_t)nt, sizeof(*w->tasks));

    if (!w->alive || !w->position || !w->velocity || !w->acceleration
        || !w->mass || !w->radius || !w->restitution || !w->age || !w->type
        || !w->disabled || !w->sleeping
        || !w->free_stack || !w->merge_list || !w->tasks) {
        physics_world_destroy(w);
        return NULL;
    }

    if (config->column_count > 0 && config->columns) {
        w->columns = malloc((size_t)config->column_count * sizeof(*w->columns));
        if (!w->columns) {
            physics_world_destroy(w);
            return NULL;
        }
        memcpy(w->columns, config->columns,
               (size_t)config->column_count * sizeof(*w->columns));
    } else {
        w->config.column_count = 0;
    }
    w->config.columns = w->columns;

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

    /* The pool only serves mutual gravity; worlds without it stay
     * single-threaded (and deterministic). */
    if (has_mutual_gravity(w)) {
        w->pool = thread_pool_create(nt);
        if (!w->pool) {
            physics_world_destroy(w);
            return NULL;
        }
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
    free(w->radius);
    free(w->restitution);
    free(w->age);
    free(w->type);
    free(w->disabled);
    free(w->sleeping);
    free(w->columns);
    free(w->free_stack);
    free(w->merge_list);
    free(w);
}

int physics_world_add(physics_world *w, const physics_body_desc *d) {
    if (!w || !d || w->free_top < 0) return -1;
    int id = w->free_stack[w->free_top--];
    w->alive[id]        = 1;
    w->position[id]     = d->position;
    w->velocity[id]     = d->velocity;
    w->acceleration[id] = (vector){0, 0, 0};
    w->mass[id]         = d->mass;
    w->radius[id]       = d->radius;
    w->restitution[id]  = d->restitution;
    w->type[id]         = (unsigned char)d->type;
    w->disabled[id]     = 0;
    w->sleeping[id]     = 0;
    w->age[id]          = 0.0f;
    w->body_count++;
    return id;
}

int physics_world_add_body(physics_world *w, vector position, vector velocity,
                           float mass) {
    physics_body_desc d = {
        .type = PHYSICS_DYNAMIC, .position = position, .velocity = velocity,
        .mass = mass, .radius = 0.0f, .restitution = 1.0f,
    };
    return physics_world_add(w, &d);
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

physics_body physics_world_body(const physics_world *w, int id) {
    physics_body b = {0};
    if (!w || !index_in_range(w, id)) return b;
    b.alive       = w->alive[id];
    b.enabled     = w->alive[id] && !w->disabled[id];
    b.type        = (physics_body_type)w->type[id];
    b.position    = w->position[id];
    b.velocity    = w->velocity[id];
    b.mass        = w->mass[id];
    b.radius      = w->radius[id];
    b.restitution = w->restitution[id];
    b.sleeping    = w->sleeping[id];
    b.age         = w->age[id];
    return b;
}

void physics_world_set_position(physics_world *w, int id, vector p) {
    if (!w || !index_in_range(w, id)) return;
    w->position[id] = p;
    w->sleeping[id] = 0;
}

void physics_world_set_velocity(physics_world *w, int id, vector v) {
    if (!w || !index_in_range(w, id)) return;
    w->velocity[id] = v;
    w->sleeping[id] = 0;
}

void physics_world_set_radius(physics_world *w, int id, float r) {
    if (!w || !index_in_range(w, id)) return;
    w->radius[id] = r;
}

void physics_world_set_mass(physics_world *w, int id, float mass) {
    if (!w || !index_in_range(w, id)) return;
    w->mass[id] = mass;
}

void physics_world_set_enabled(physics_world *w, int id, int enabled) {
    if (!w || !index_in_range(w, id) || !w->alive[id]) return;
    if (enabled && w->disabled[id]) {
        w->age[id]      = 0.0f;
        w->sleeping[id] = 0;
    }
    w->disabled[id] = !enabled;
}

/* Returns the index of the last column `p` was pushed out of, or -1. */
static int push_out_columns(const physics_world *w, vector *p, float r,
                            vector *normal) {
    int last = -1;
    *normal = (vector){0, 0, 0};
    for (int i = 0; i < w->config.column_count; i++) {
        const physics_column *c = &w->columns[i];
        float dx = p->x - c->x, dz = p->z - c->z;
        float d = sqrtf(dx * dx + dz * dz), min = c->r + r;
        if (d < min && d > 1e-5f) {
            float nx = dx / d, nz = dz / d;
            p->x = c->x + nx * min;
            p->z = c->z + nz * min;
            *normal = (vector){nx, 0, nz};
            last = i;
        }
    }
    return last;
}

vector physics_world_push_out_columns(const physics_world *w, vector *p,
                                      float r) {
    vector n = {0, 0, 0};
    if (w && p) push_out_columns(w, p, r, &n);
    return n;
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
        if (!active(w, i, PHYSICS_DYNAMIC)) continue;

        for (int j = i + 1; j < w->capacity; j++) {
            if (!active(w, j, PHYSICS_DYNAMIC)) continue;

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

/* ------------------------------------------------------------------ */
/* Collisions                                                          */
/* ------------------------------------------------------------------ */

static void report(physics_world *w, physics_contact_kind kind, int body,
                   int other, vector normal, float speed, float impulse) {
    if (!w->config.on_contact) return;
    physics_contact c = {kind, body, other, normal, speed, impulse};
    w->config.on_contact(w->config.contact_user, w, &c);
}

static float inv_mass(const physics_world *w, int id) {
    return w->mass[id] > 0.0f ? 1.0f / w->mass[id] : 0.0f;
}

static void ground_contact(physics_world *w, int i, float h) {
    const physics_config *c = &w->config;
    vector *p = &w->position[i], *v = &w->velocity[i];
    float r = w->radius[i];
    if (p->y < c->ground_y + r) {
        p->y = c->ground_y + r;
        if (v->y < 0) {
            float e = fminf(w->restitution[i], c->ground_restitution);
            float imp = -v->y;
            v->y = imp > c->ground_settle_speed ? imp * e : 0.0f;
            v->x *= c->ground_grip;
            v->z *= c->ground_grip;
            report(w, PHYSICS_CONTACT_GROUND, i, -1, (vector){0, 1, 0}, imp, 0.0f);
        }
    }
    /* Rolling: on the ground with no vertical motion. */
    if (p->y <= c->ground_y + r + 1e-4f && v->y == 0.0f) {
        float k = expf(-c->roll_friction * h);
        v->x *= k;
        v->z *= k;
        if (v->x * v->x + v->z * v->z < c->sleep_speed * c->sleep_speed) {
            *v = (vector){0, 0, 0};
            w->sleeping[i] = 1;
        }
    }
}

/* One wall along one axis: coordinate *pc must satisfy sign * *pc <= lim. */
static void wall_contact(physics_world *w, int i, float *pc, float *vc,
                         float lim, int sign, int which) {
    if (sign * *pc <= lim) return;
    *pc = sign * lim;
    if (sign * *vc > 0) {
        float imp = fabsf(*vc);
        *vc = -*vc * fminf(w->restitution[i], w->config.wall_restitution);
        vector n = {0, 0, 0};
        if (which < 2) n.x = (float)-sign; else n.z = (float)-sign;
        report(w, PHYSICS_CONTACT_WALL, i, which, n, imp, 0.0f);
    }
}

static void column_contact(physics_world *w, int i) {
    vector n;
    int col = push_out_columns(w, &w->position[i], w->radius[i], &n);
    if (col < 0) return;
    vector *v = &w->velocity[i];
    float vn = v->x * n.x + v->z * n.z;
    if (vn >= 0) return;
    float e = fminf(w->restitution[i], w->columns[col].restitution);
    v->x -= (1.0f + e) * vn * n.x;
    v->z -= (1.0f + e) * vn * n.z;
    report(w, PHYSICS_CONTACT_COLUMN, i, col, n, -vn,
           -(1.0f + e) * vn * w->mass[i]);
}

/* Dynamic sphere i against every enabled kinematic sphere. Only i moves;
 * the contact carries the impulse so the caller can react. */
static void kinematic_contacts(physics_world *w, int i) {
    for (int k = 0; k < w->capacity; k++) {
        if (!active(w, k, PHYSICS_KINEMATIC)) continue;
        vector d = vector_sub(w->position[i], w->position[k]);
        float dist = vector_magnitude(d), min = w->radius[k] + w->radius[i];
        if (dist >= min || dist < 1e-5f) continue;
        vector n = vector_scale(d, 1.0f / dist);
        w->position[i] = vector_add(w->position[k], vector_scale(n, min));
        float vn = vector_dot(vector_sub(w->velocity[i], w->velocity[k]), n);
        if (vn >= 0) continue;
        float e = fminf(w->restitution[i], w->restitution[k]);
        float inv_i = inv_mass(w, i);
        float j = -(1.0f + e) * vn / (inv_i + inv_mass(w, k));
        w->velocity[i] = vector_add(w->velocity[i], vector_scale(n, j * inv_i));
        w->sleeping[i] = 0;
        report(w, PHYSICS_CONTACT_BODY, i, k, n, -vn, j);
    }
}

/* Every pair of enabled dynamic spheres (sleeping ones included). */
static void dynamic_pairs(physics_world *w) {
    for (int i = 0; i < w->capacity; i++) {
        if (!active(w, i, PHYSICS_DYNAMIC) || w->radius[i] <= 0.0f) continue;
        for (int k = i + 1; k < w->capacity; k++) {
            if (!active(w, k, PHYSICS_DYNAMIC) || w->radius[k] <= 0.0f) continue;
            vector d = vector_sub(w->position[k], w->position[i]);
            float dist = vector_magnitude(d);
            float min = w->radius[i] + w->radius[k];
            if (dist >= min || dist < 1e-5f) continue;
            vector n = vector_scale(d, 1.0f / dist);
            float inv_i = inv_mass(w, i), inv_k = inv_mass(w, k);
            float inv_sum = inv_i + inv_k;
            if (inv_sum <= 0.0f) continue;
            float overlap = min - dist;
            w->position[i] = vector_sub(w->position[i], vector_scale(n, overlap * (inv_i / inv_sum)));
            w->position[k] = vector_add(w->position[k], vector_scale(n, overlap * (inv_k / inv_sum)));
            float vn = vector_dot(vector_sub(w->velocity[k], w->velocity[i]), n);
            if (vn >= 0) continue;
            float e = fminf(w->restitution[i], w->restitution[k]);
            float j = -(1.0f + e) * vn / inv_sum;
            w->velocity[i] = vector_sub(w->velocity[i], vector_scale(n, j * inv_i));
            w->velocity[k] = vector_add(w->velocity[k], vector_scale(n, j * inv_k));
            w->sleeping[i] = w->sleeping[k] = 0;
            report(w, PHYSICS_CONTACT_BODY, k, i, n, -vn, j);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Stepping                                                            */
/* ------------------------------------------------------------------ */

static void substep(physics_world *w, float h) {
    const physics_config *c = &w->config;
    int mutual = has_mutual_gravity(w);
    if (mutual) {
        reset_accelerations(w);
        accumulate_forces(w);
        apply_merges(w);
    }

    for (int i = 0; i < w->capacity; i++) {
        if (!active(w, i, PHYSICS_DYNAMIC)) continue;
        w->age[i] += h;
        if (w->sleeping[i]) continue;

        if (mutual)
            w->velocity[i] = vector_add(w->velocity[i],
                                        vector_scale(w->acceleration[i], h));
        if (c->fall_gravity != 0.0f) w->velocity[i].y -= c->fall_gravity * h;
        w->position[i] = vector_add(w->position[i], vector_scale(w->velocity[i], h));

        if (c->bounded) reflect_body(w, i);
        if (!c->collisions) continue;

        if (c->ground) ground_contact(w, i, h);
        if (c->walls) {
            float r = w->radius[i];
            vector *p = &w->position[i], *v = &w->velocity[i];
            wall_contact(w, i, &p->x, &v->x,   c->wall_max_x - r,  1, 0);
            wall_contact(w, i, &p->x, &v->x, -(c->wall_min_x) - r, -1, 1);
            wall_contact(w, i, &p->z, &v->z,   c->wall_max_z - r,  1, 2);
            wall_contact(w, i, &p->z, &v->z, -(c->wall_min_z) - r, -1, 3);
        }
        column_contact(w, i);
        kinematic_contacts(w, i);
    }

    if (c->collisions) dynamic_pairs(w);
}

int physics_world_advance(physics_world *w, float dt) {
    if (!w) return 0;
    const physics_config *c = &w->config;
    int steps = 1;
    if (c->max_substep_dist > 0.0f) {
        float vmax = 0.0f;
        for (int i = 0; i < w->capacity; i++)
            if (active(w, i, PHYSICS_DYNAMIC) && !w->sleeping[i])
                vmax = fmaxf(vmax, vector_magnitude(w->velocity[i]));
        steps = (int)ceilf(vmax * dt / c->max_substep_dist);
        if (steps < c->min_substeps) steps = c->min_substeps;
        if (steps > c->max_substeps) steps = c->max_substeps;
        if (steps < 1) steps = 1;
    }
    float h = dt / (float)steps;
    for (int s = 0; s < steps; s++) substep(w, h);
    return steps;
}

void physics_world_step(physics_world *w) {
    if (w) physics_world_advance(w, w->config.dt);
}
