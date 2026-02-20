#ifndef NBODY_H
#define NBODY_H

/**
 * Initialize the simulation state (entity pool).
 */
void nbody_init(void);

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

#endif /* NBODY_H */
