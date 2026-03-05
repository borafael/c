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
 * Present the rendered frame.
 */
void render_present(void);

/**
 * Add a small delay (for frame pacing).
 */
void render_delay(int ms);

/**
 * Create a streaming texture (ARGB8888). Returns opaque handle.
 */
void *render_create_texture(int width, int height);

/**
 * Update the texture from a pixel buffer and copy it to the screen.
 * pitch is the number of bytes per row (width * 4 for ARGB8888).
 */
void render_texture_update(void *texture, const void *pixels, int pitch);

/**
 * Destroy a texture created with render_create_texture.
 */
void render_destroy_texture(void *texture);

/**
 * Clean up rendering resources.
 */
void render_cleanup(void);

#endif /* RENDER_H */
