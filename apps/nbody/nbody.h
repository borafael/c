#ifndef NBODY_H
#define NBODY_H

#include "input.h"

/**
 * Simulation configuration.
 * Use nbody_default_config() to get defaults, then override as needed.
 */
typedef struct {
    int num_entities;
    float gravity;
    float dt;
    float world_radius;
    float softening;
    int num_threads;
    float rotation_speed;
    int use_gpu;
    int bounded;
} nbody_config;

/**
 * Return the default configuration values.
 */
nbody_config nbody_default_config(void);

/**
 * Initialize the simulation state with the given configuration.
 */
void nbody_init(const nbody_config *config);

/**
 * Spawn initial entities.
 */
void nbody_spawn_entities(void);

/**
 * Reset and respawn all entities.
 */
void nbody_reset(void);

/**
 * Update simulation state (physics).
 */
void nbody_update(void);

/**
 * Render all entities.
 * Requires screen dimensions to map world to screen coordinates.
 */
void nbody_render(int screen_width, int screen_height);

/**
 * Handle input events (camera movement, speed, reset).
 */
void nbody_handle_input(const input_events *events);

/**
 * Clean up simulation resources (thread pool).
 */
void nbody_cleanup(void);

#endif /* NBODY_H */
