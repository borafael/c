#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

/**
 * Initialize the rendering system.
 * Returns 0 on success, -1 on failure.
 */
int render_init(void);

/**
 * Get the current screen dimensions.
 */
void render_get_size(int* width, int* height);

/**
 * Clear the screen to black.
 */
void render_clear(void);

/**
 * Draw a filled circle.
 */
void render_circle(int x, int y, int radius, uint8_t r, uint8_t g, uint8_t b);

/**
 * Present the rendered frame.
 */
void render_present(void);

/**
 * Add a small delay (for frame pacing).
 */
void render_delay(int ms);

/**
 * Clean up rendering resources.
 */
void render_cleanup(void);

#endif /* RENDER_H */
