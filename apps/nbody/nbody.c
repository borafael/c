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
#define NUM_ENTITIES 8000
#define G 0.5f
#define DT 0.016f
#define WORLD_WIDTH 800.0f
#define WORLD_HEIGHT 400.0f
#define SOFTENING 5.0f

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

#define NUM_THREADS 8

typedef struct {
    int start;
    int end;
    vector local_accel[MAX_ENTITIES];
    merge_pair local_merges[MAX_MERGES / NUM_THREADS];
    int merge_count;
} force_task_args;

static thread_pool *pool;
static force_task_args task_args[NUM_THREADS];

static int bounds_enabled = 0;
static float zoom = 1.0f;
static float time_scale = 1.0f;
static float camera_offset_x = 0.0f;
static float camera_offset_y = 0.0f;
#define PAN_SPEED 10.0f

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

void nbody_zoom_in(void) {
    zoom *= 1.1f;
    if (zoom > 20.0f) zoom = 20.0f;
}

void nbody_zoom_out(void) {
    zoom /= 1.1f;
    if (zoom < 0.1f) zoom = 0.1f;
}

void nbody_speed_up(void) {
    time_scale *= 1.5f;
    if (time_scale > 10.0f) time_scale = 10.0f;
}

void nbody_speed_down(void) {
    time_scale /= 1.5f;
    if (time_scale < 0.1f) time_scale = 0.1f;
}

void nbody_pan_up(void) {
    camera_offset_y += PAN_SPEED / zoom;
}

void nbody_pan_down(void) {
    camera_offset_y -= PAN_SPEED / zoom;
}

void nbody_pan_left(void) {
    camera_offset_x += PAN_SPEED / zoom;
}

void nbody_pan_right(void) {
    camera_offset_x -= PAN_SPEED / zoom;
}

void nbody_init(void) {
    for (int i = MAX_ENTITIES - 1; i >= 0; i--) {
        free_stack[++top] = i;
    }
    pool = thread_pool_create(NUM_THREADS);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool\n");
        exit(EXIT_FAILURE);
    }
}

void nbody_spawn_entities(void) {
    srand(time(NULL));
    for (int i = 0; i < NUM_ENTITIES; i++) {
        int id = create_entity();
        if (id < 0) break;

        entity_masks[id] = POSITION | PHYSICS;

        position_components[id].coordinates.x = (float)(rand() % (int)WORLD_WIDTH);
        position_components[id].coordinates.y = (float)(rand() % (int)WORLD_HEIGHT);

        physics_components[id].velocity.x = ((float)rand() / RAND_MAX - 0.5f) * 10.0f;
        physics_components[id].velocity.y = ((float)rand() / RAND_MAX - 0.5f) * 10.0f;
        physics_components[id].acceleration.x = 0;
        physics_components[id].acceleration.y = 0;
        physics_components[id].mass = 1.0f + (float)(rand() % 10);
    }
}

void nbody_reset(void) {
    zoom = 1.0f;
    time_scale = 1.0f;
    camera_offset_x = 0.0f;
    camera_offset_y = 0.0f;
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

            if (dist < SOFTENING) {
                int max_local = MAX_MERGES / NUM_THREADS;
                if (a->merge_count < max_local) {
                    a->local_merges[a->merge_count].i = i;
                    a->local_merges[a->merge_count].j = j;
                    a->merge_count++;
                }
                continue;
            }

            float force = G * physics_components[i].mass
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
    int chunk = MAX_ENTITIES / NUM_THREADS;
    for (int t = 0; t < NUM_THREADS; t++) {
        task_args[t].start = t * chunk;
        task_args[t].end = (t == NUM_THREADS - 1) ? MAX_ENTITIES : (t + 1) * chunk;
        thread_pool_submit(pool, compute_forces_chunk, &task_args[t]);
    }
    thread_pool_wait(pool);

    /* Sum thread-local accelerations */
    for (int t = 0; t < NUM_THREADS; t++) {
        for (int i = 0; i < MAX_ENTITIES; i++) {
            physics_components[i].acceleration = vector_add(
                physics_components[i].acceleration, task_args[t].local_accel[i]);
        }
    }

    /* Collect merge candidates from all threads */
    int merge_count = 0;
    for (int t = 0; t < NUM_THREADS; t++) {
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
            vector_scale(physics_components[i].acceleration, DT));

        position_components[i].coordinates = vector_add(
            position_components[i].coordinates,
            vector_scale(physics_components[i].velocity, DT));

        /* Boundary collision */
        if (bounds_enabled) {
            if (position_components[i].coordinates.x < 0) {
                position_components[i].coordinates.x = 0;
                physics_components[i].velocity.x *= -0.5f;
            }
            if (position_components[i].coordinates.x > WORLD_WIDTH) {
                position_components[i].coordinates.x = WORLD_WIDTH;
                physics_components[i].velocity.x *= -0.5f;
            }
            if (position_components[i].coordinates.y < 0) {
                position_components[i].coordinates.y = 0;
                physics_components[i].velocity.y *= -0.5f;
            }
            if (position_components[i].coordinates.y > WORLD_HEIGHT) {
                position_components[i].coordinates.y = WORLD_HEIGHT;
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

    float camera_x = (WORLD_WIDTH / 2.0f - camera_offset_x) - (WORLD_WIDTH / 2.0f) / zoom;
    float camera_y = (WORLD_HEIGHT / 2.0f - camera_offset_y) - (WORLD_HEIGHT / 2.0f) / zoom;

    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & POSITION) != POSITION)
            continue;

        int sx = (int)((position_components[i].coordinates.x - camera_x) * zoom / WORLD_WIDTH * screen_width);
        int sy = (int)((position_components[i].coordinates.y - camera_y) * zoom / WORLD_HEIGHT * screen_height);

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
