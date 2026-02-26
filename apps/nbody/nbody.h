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

/**
 * Zoom in (increase zoom level).
 */
void nbody_zoom_in(void);

/**
 * Zoom out (decrease zoom level).
 */
void nbody_zoom_out(void);

/**
 * Speed up simulation.
 */
void nbody_speed_up(void);

/**
 * Slow down simulation.
 */
void nbody_speed_down(void);

/**
 * Pan camera up.
 */
void nbody_pan_up(void);

/**
 * Pan camera down.
 */
void nbody_pan_down(void);

/**
 * Pan camera left.
 */
void nbody_pan_left(void);

/**
 * Pan camera right.
 */
void nbody_pan_right(void);

/**
 * Clean up simulation resources (thread pool).
 */
void nbody_cleanup(void);

#endif /* NBODY_H */
