#include "nbody.h"
#include "render.h"
#include "vector.h"
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define MAX_ENTITIES 2000
#define NUM_ENTITIES 2000
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

static int free_stack[MAX_ENTITIES];
static int top = -1;

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

void nbody_init(void) {
    for (int i = MAX_ENTITIES - 1; i >= 0; i--) {
        free_stack[++top] = i;
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
    for (int i = 0; i < MAX_ENTITIES; i++) {
        entity_masks[i] = NONE;
    }
    top = -1;
    for (int i = MAX_ENTITIES - 1; i >= 0; i--) {
        free_stack[++top] = i;
    }
    nbody_spawn_entities();
}

void nbody_update(void) {
    /* Reset accelerations */
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;

        physics_components[i].acceleration.x = 0;
        physics_components[i].acceleration.y = 0;
    }

    /* Calculate gravitational forces */
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;

        for (int j = i + 1; j < MAX_ENTITIES; j++) {
            if ((entity_masks[j] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
                continue;

            vector diff = vector_sub(position_components[j].coordinates,
                                     position_components[i].coordinates);
            float dist = vector_magnitude(diff);

            /* Merge entities if too close */
            if (dist < SOFTENING) {
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
                continue;
            }

            float force = G * physics_components[i].mass * physics_components[j].mass / (dist * dist);
            vector dir = vector_scale(diff, 1.0f / dist);
            vector force_vec = vector_scale(dir, force);

            physics_components[i].acceleration = vector_add(
                physics_components[i].acceleration,
                vector_scale(force_vec, 1.0f / physics_components[i].mass));

            physics_components[j].acceleration = vector_sub(
                physics_components[j].acceleration,
                vector_scale(force_vec, 1.0f / physics_components[j].mass));
        }
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

void nbody_render(int screen_width, int screen_height) {
    render_clear();

    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & POSITION) != POSITION)
            continue;

        int sx = (int)(position_components[i].coordinates.x / WORLD_WIDTH * screen_width);
        int sy = (int)(position_components[i].coordinates.y / WORLD_HEIGHT * screen_height);

        if (sx >= 0 && sx < screen_width && sy >= 0 && sy < screen_height) {
            int radius = 2;
            uint8_t r = 100, g = 100, b = 255;

            if ((entity_masks[i] & PHYSICS) == PHYSICS) {
                float mass = physics_components[i].mass;
                float t = logf(mass) / logf(1000.0f);
                if (t < 0) t = 0;
                if (t > 1) t = 1;

                r = (uint8_t)(50 + t * 205);
                g = (uint8_t)(50 * (1 - t * t));
                b = (uint8_t)(255 * (1 - t * t));

                radius = 2 + (int)(logf(mass) * 2.0f);
            }

            render_circle(sx, sy, radius, r, g, b);
        }
    }

    render_present();
}
