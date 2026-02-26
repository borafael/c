#include "nbody.h"
#include "render.h"
#include "vector.h"
#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAX_ENTITIES 8000
#define MAX_THREADS 64

static int num_entities = 8000;
static float gravity = 0.5f;
static float dt = 0.016f;
static float world_radius = 400.0f;
static float softening = 5.0f;
static int num_threads = 8;
static float rotation_speed = 0.05f;

typedef enum {
    NONE     = 0,
    POSITION = (1 << 0),
    PHYSICS  = (1 << 1)
} component_type;

typedef struct {
    vector coordinates;
} position_component;

typedef struct {
    vector velocity;
    vector acceleration;
    float mass;
} physics_component;

static unsigned int entity_masks[MAX_ENTITIES] = {0};
static position_component position_components[MAX_ENTITIES];
static physics_component physics_components[MAX_ENTITIES];

typedef struct {
    int i;
    int j;
} merge_pair;

static int free_stack[MAX_ENTITIES];
static int top = -1;

#define MAX_MERGES (MAX_ENTITIES / 2)
static merge_pair merge_list[MAX_MERGES];

typedef struct {
    int start;
    int end;
    vector local_accel[MAX_ENTITIES];
    merge_pair local_merges[MAX_MERGES];
    int merge_count;
} force_task_args;

static thread_pool *pool;
static force_task_args task_args[MAX_THREADS];

static int bounds_enabled = 0;
static float camera_azimuth = 0.0f;
static float camera_elevation = 0.3f;
static float camera_distance = 800.0f;
static float time_scale = 1.0f;

static int create_entity(void) {
    if (top >= 0) {
        return free_stack[top--];
    }
    return -1;
}

static void destroy_entity(int id) {
    entity_masks[id] = NONE;
    free_stack[++top] = id;
}

void nbody_set_bounds(int enabled) {
    bounds_enabled = enabled;
}

void nbody_distance_increase(void) {
    camera_distance *= 1.1f;
    if (camera_distance > 5000.0f) camera_distance = 5000.0f;
}

void nbody_distance_decrease(void) {
    camera_distance /= 1.1f;
    if (camera_distance < 50.0f) camera_distance = 50.0f;
}

void nbody_rotate_left(void) {
    camera_azimuth -= rotation_speed;
}

void nbody_rotate_right(void) {
    camera_azimuth += rotation_speed;
}

void nbody_rotate_up(void) {
    camera_elevation += rotation_speed;
    if (camera_elevation > 1.5f) camera_elevation = 1.5f;
}

void nbody_rotate_down(void) {
    camera_elevation -= rotation_speed;
    if (camera_elevation < -1.5f) camera_elevation = -1.5f;
}

nbody_config nbody_default_config(void) {
    nbody_config config;
    config.num_entities = 8000;
    config.gravity = 0.5f;
    config.dt = 0.016f;
    config.world_radius = 400.0f;
    config.softening = 5.0f;
    config.num_threads = 8;
    config.rotation_speed = 0.05f;
    return config;
}

void nbody_init(const nbody_config *config) {
    num_entities = config->num_entities;
    if (num_entities > MAX_ENTITIES) {
        fprintf(stderr, "Warning: num_entities %d exceeds max %d, clamping\n",
                num_entities, MAX_ENTITIES);
        num_entities = MAX_ENTITIES;
    }
    gravity = config->gravity;
    dt = config->dt;
    world_radius = config->world_radius;
    softening = config->softening;
    num_threads = config->num_threads;
    if (num_threads > MAX_THREADS) {
        fprintf(stderr, "Warning: num_threads %d exceeds max %d, clamping\n",
                num_threads, MAX_THREADS);
        num_threads = MAX_THREADS;
    }
    if (num_threads < 1) num_threads = 1;
    rotation_speed = config->rotation_speed;

    for (int i = MAX_ENTITIES - 1; i >= 0; i--) {
        free_stack[++top] = i;
    }
    pool = thread_pool_create(num_threads);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool\n");
        exit(EXIT_FAILURE);
    }
}

void nbody_spawn_entities(void) {
    srand(time(NULL));
    for (int i = 0; i < num_entities; i++) {
        int id = create_entity();
        if (id < 0) break;

        entity_masks[id] = POSITION | PHYSICS;

        position_components[id].coordinates.x = (float)(rand() % (int)world_width);
        position_components[id].coordinates.y = (float)(rand() % (int)world_height);

        physics_components[id].velocity.x = ((float)rand() / RAND_MAX - 0.5f) * 10.0f;
        physics_components[id].velocity.y = ((float)rand() / RAND_MAX - 0.5f) * 10.0f;
        physics_components[id].acceleration.x = 0;
        physics_components[id].acceleration.y = 0;
        physics_components[id].mass = 1.0f + (float)(rand() % 10);
    }
}

void nbody_reset(void) {
    camera_azimuth = 0.0f;
    camera_elevation = 0.3f;
    camera_distance = 800.0f;
    time_scale = 1.0f;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        entity_masks[i] = NONE;
    }
    top = -1;
    for (int i = MAX_ENTITIES - 1; i >= 0; i--) {
        free_stack[++top] = i;
    }
    nbody_spawn_entities();
}

void nbody_cleanup(void) {
    if (pool) {
        thread_pool_destroy(pool);
        pool = NULL;
    }
}

static void compute_forces_chunk(void *arg) {
    force_task_args *a = (force_task_args *)arg;

    memset(a->local_accel, 0, sizeof(a->local_accel));
    a->merge_count = 0;

    for (int i = a->start; i < a->end; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;

        for (int j = i + 1; j < MAX_ENTITIES; j++) {
            if ((entity_masks[j] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
                continue;

            vector diff = vector_sub(position_components[j].coordinates,
                                     position_components[i].coordinates);
            float dist = vector_magnitude(diff);

            if (dist < softening) {
                int max_local = MAX_MERGES / num_threads;
                if (a->merge_count < max_local) {
                    a->local_merges[a->merge_count].i = i;
                    a->local_merges[a->merge_count].j = j;
                    a->merge_count++;
                }
                continue;
            }

            float force = gravity * physics_components[i].mass
                        * physics_components[j].mass / (dist * dist);
            vector dir = vector_scale(diff, 1.0f / dist);
            vector force_vec = vector_scale(dir, force);

            a->local_accel[i] = vector_add(a->local_accel[i],
                vector_scale(force_vec, 1.0f / physics_components[i].mass));
            a->local_accel[j] = vector_sub(a->local_accel[j],
                vector_scale(force_vec, 1.0f / physics_components[j].mass));
        }
    }
}

static void nbody_step(void) {
    /* Reset accelerations */
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;

        physics_components[i].acceleration.x = 0;
        physics_components[i].acceleration.y = 0;
    }

    /* Calculate gravitational forces (parallel) */
    int chunk = MAX_ENTITIES / num_threads;
    for (int t = 0; t < num_threads; t++) {
        task_args[t].start = t * chunk;
        task_args[t].end = (t == num_threads - 1) ? MAX_ENTITIES : (t + 1) * chunk;
        thread_pool_submit(pool, compute_forces_chunk, &task_args[t]);
    }
    thread_pool_wait(pool);

    /* Sum thread-local accelerations */
    for (int t = 0; t < num_threads; t++) {
        for (int i = 0; i < MAX_ENTITIES; i++) {
            physics_components[i].acceleration = vector_add(
                physics_components[i].acceleration, task_args[t].local_accel[i]);
        }
    }

    /* Collect merge candidates from all threads */
    int merge_count = 0;
    for (int t = 0; t < num_threads; t++) {
        for (int m = 0; m < task_args[t].merge_count; m++) {
            if (merge_count < MAX_MERGES) {
                merge_list[merge_count++] = task_args[t].local_merges[m];
            }
        }
    }

    /* Apply merges */
    for (int m = 0; m < merge_count; m++) {
        int i = merge_list[m].i;
        int j = merge_list[m].j;

        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;
        if ((entity_masks[j] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;

        float m_i = physics_components[i].mass;
        float m_j = physics_components[j].mass;
        float total_mass = m_i + m_j;

        position_components[i].coordinates = vector_scale(
            vector_add(vector_scale(position_components[i].coordinates, m_i),
                       vector_scale(position_components[j].coordinates, m_j)),
            1.0f / total_mass);

        physics_components[i].velocity = vector_scale(
            vector_add(vector_scale(physics_components[i].velocity, m_i),
                       vector_scale(physics_components[j].velocity, m_j)),
            1.0f / total_mass);

        physics_components[i].mass = total_mass;

        destroy_entity(j);
    }

    /* Integrate velocity and position */
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;

        physics_components[i].velocity = vector_add(
            physics_components[i].velocity,
            vector_scale(physics_components[i].acceleration, dt));

        position_components[i].coordinates = vector_add(
            position_components[i].coordinates,
            vector_scale(physics_components[i].velocity, dt));

        /* Boundary collision */
        if (bounds_enabled) {
            if (position_components[i].coordinates.x < 0) {
                position_components[i].coordinates.x = 0;
                physics_components[i].velocity.x *= -0.5f;
            }
            if (position_components[i].coordinates.x > world_width) {
                position_components[i].coordinates.x = world_width;
                physics_components[i].velocity.x *= -0.5f;
            }
            if (position_components[i].coordinates.y < 0) {
                position_components[i].coordinates.y = 0;
                physics_components[i].velocity.y *= -0.5f;
            }
            if (position_components[i].coordinates.y > world_height) {
                position_components[i].coordinates.y = world_height;
                physics_components[i].velocity.y *= -0.5f;
            }
        }
    }
}

void nbody_update(void) {
    int steps = (int)ceilf(time_scale);
    for (int s = 0; s < steps; s++) {
        nbody_step();
    }
}

void nbody_render(int screen_width, int screen_height) {
    render_clear();

    float camera_x = (world_width / 2.0f - camera_offset_x) - (world_width / 2.0f) / zoom;
    float camera_y = (world_height / 2.0f - camera_offset_y) - (world_height / 2.0f) / zoom;

    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & POSITION) != POSITION)
            continue;

        int sx = (int)((position_components[i].coordinates.x - camera_x) * zoom / world_width * screen_width);
        int sy = (int)((position_components[i].coordinates.y - camera_y) * zoom / world_height * screen_height);

        if (sx >= 0 && sx < screen_width && sy >= 0 && sy < screen_height) {
            int radius = (int)(2 * sqrtf(zoom));
            uint8_t r = 100, g = 100, b = 255;

            if ((entity_masks[i] & PHYSICS) == PHYSICS) {
                float mass = physics_components[i].mass;
                float t = logf(mass) / logf(1000.0f);
                if (t < 0) t = 0;
                if (t > 1) t = 1;

                r = (uint8_t)(50 + t * 205);
                g = (uint8_t)(50 * (1 - t * t));
                b = (uint8_t)(255 * (1 - t * t));

                radius = (int)((2 + (int)(logf(mass) * 2.0f)) * sqrtf(zoom));
            }

            render_circle(sx, sy, radius, r, g, b);
        }
    }

    render_present();
}
