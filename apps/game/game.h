#ifndef GAME_H
#define GAME_H

/**
 * Initialize the game state (entity pool).
 */
void game_init(void);

/**
 * Spawn initial entities.
 */
void game_spawn_entities(void);

/**
 * Reset and respawn all entities.
 */
void game_reset(void);

/**
 * Update game state (physics simulation).
 */
void game_update(void);

/**
 * Render all entities.
 * Requires screen dimensions to map world to screen coordinates.
 */
void game_render(int screen_width, int screen_height);

#endif /* GAME_H */
