#include "scene.h"
#include <stdio.h>
#include <stdlib.h>

/* stb_image is also implemented in libs/slice/slice.c; declaring it
 * static here keeps its symbols local to this translation unit so
 * apps that link both libslice and libscene don't see duplicates. */
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

int scene_texture_load(const char *path, scene_texture *out) {
    if (!path || !out) return -1;

    int w, h, channels;
    unsigned char *rgba = stbi_load(path, &w, &h, &channels, 4);
    if (!rgba) {
        fprintf(stderr, "scene_texture_load: cannot load '%s': %s\n",
                path, stbi_failure_reason());
        return -1;
    }

    uint32_t *pixels = malloc(sizeof(uint32_t) * (size_t)w * (size_t)h);
    if (!pixels) {
        stbi_image_free(rgba);
        return -1;
    }

    for (int i = 0; i < w * h; i++) {
        unsigned char *p = &rgba[i * 4];
        pixels[i] = ((uint32_t)p[3] << 24)   /* A */
                  | ((uint32_t)p[0] << 16)   /* R */
                  | ((uint32_t)p[1] <<  8)   /* G */
                  |  (uint32_t)p[2];         /* B */
    }
    stbi_image_free(rgba);

    out->pixels = pixels;
    out->width  = w;
    out->height = h;
    return 0;
}

void scene_texture_free(scene_texture *t) {
    if (!t) return;
    free(t->pixels);
    t->pixels = NULL;
    t->width  = 0;
    t->height = 0;
}
