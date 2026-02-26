#ifndef NBODY_H
#define NBODY_H

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
 * Enable or disable boundary collision.
 * Disabled by default.
 */
void nbody_set_bounds(int enabled);

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
 * Increase camera distance from center.
 */
void nbody_distance_increase(void);

/**
 * Decrease camera distance from center.
 */
void nbody_distance_decrease(void);

/**
 * Rotate camera azimuth left.
 */
void nbody_rotate_left(void);

/**
 * Rotate camera azimuth right.
 */
void nbody_rotate_right(void);

/**
 * Rotate camera elevation up.
 */
void nbody_rotate_up(void);

/**
 * Rotate camera elevation down.
 */
void nbody_rotate_down(void);

/**
 * Clean up simulation resources (thread pool).
 */
void nbody_cleanup(void);

#endif /* NBODY_H */
