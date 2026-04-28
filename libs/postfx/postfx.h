#ifndef POSTFX_H
#define POSTFX_H

#include <stdint.h>

/**
 * CPU post-processing passes that operate on a finished colour buffer
 * (and, for edge detection, an associated G-buffer). All passes mutate
 * the pixel buffer in place; ARGB8888, row-major, width*height entries.
 *
 * These are intentionally renderer-agnostic — postfx_gbuffer is a thin
 * view struct so callers can wire up any source of object/depth/normal
 * data, not just rt_gbuffer. For raytrace clients, copy the pointers
 * across at the call site; layouts already match.
 */

/* ===================================================================
 *   Edge detection (comic outlines)
 * =================================================================== */

/**
 * Per-pixel geometry buffer consumed by postfx_apply_edges. All three
 * channels are width*height entries; normal is interleaved xyz so 3*N
 * floats. NULL channels are tolerated as long as the matching use_*
 * flag is off.
 */
typedef struct {
    const uint32_t *object_id;
    const float    *depth;
    const float    *normal;
} postfx_gbuffer;

/**
 * Edge-detection thresholds. Each use_* toggle independently enables
 * one source of edges; combine for richer outlines. depth_threshold is
 * in world units (the pass scales it by depth so a small gap far away
 * does not outline). normal_threshold is the dot-product floor below
 * which two adjacent normals count as a crease (1.0 = identical).
 */
typedef struct {
    int   use_object_id;
    int   use_depth;
    int   use_normal;
    int   eight_connected;   /* 1 = 8-neighbour, 0 = 4-neighbour */
    float depth_threshold;
    float normal_threshold;
} postfx_edges;

/**
 * Stamp black wherever the G-buffer signals an edge. pixels is mutated
 * in place; non-edge pixels are left untouched.
 */
void postfx_apply_edges(uint32_t *pixels,
                        const postfx_gbuffer *gbuf,
                        int width, int height,
                        const postfx_edges *cfg);

/* ===================================================================
 *   Palette quantization (pixel-art look)
 * =================================================================== */

typedef struct { uint8_t r, g, b; } postfx_rgb;

typedef struct {
    const postfx_rgb *colors;
    int               count;
    const char       *name;
} postfx_palette;

/**
 * Built-in curated palette table, ordered low to high colour count.
 * Indices are stable across releases; use postfx_palette_count() to
 * size loops. Names are stable lower-case identifiers ("bw2", "gb4",
 * "edg32", ...).
 */
const postfx_palette *postfx_palette_at(int index);
int                    postfx_palette_count(void);

/**
 * Quantize each pixel to its nearest palette entry in sRGB space. If
 * dither is non-zero, applies a 4x4 Bayer offset before the lookup —
 * worth turning on for small palettes (≤16) where banding is harsh.
 */
void postfx_quantize(uint32_t *pixels, int width, int height,
                     const postfx_palette *pal, int dither);

/* ===================================================================
 *   Luminance posterize (cel-shading approximation)
 * =================================================================== */

/**
 * Snap luminance to a fixed number of bands while preserving chroma.
 * Cheap stand-in for true cel-shading — bands the brightness while
 * keeping hue stable.
 */
void postfx_posterize(uint32_t *pixels, int width, int height);

/* ===================================================================
 *   Bloom (bright-pass + downsampled separable Gaussian)
 * =================================================================== */

/* Ctx owns the half-res scratch buffers. The pass downsamples once,
 * blurs, then upsamples and adds back, so we keep the float scratch
 * around between frames instead of reallocating every call. The
 * apply function transparently resizes if width/height change. */
typedef struct postfx_bloom_ctx postfx_bloom_ctx;

postfx_bloom_ctx *postfx_bloom_create(int width, int height);
void              postfx_bloom_destroy(postfx_bloom_ctx *ctx);

/* threshold/knee are LDR luminance (0..1). intensity scales the
 * additive bright contribution. radius is the Gaussian half-width in
 * low-res pixels (clamped to 1..16). iterations applies the separable
 * blur N times for a wider, softer falloff (clamped to 1..4). */
typedef struct {
    int   enabled;
    float threshold;
    float knee;
    float intensity;
    int   radius;
    int   iterations;
} postfx_bloom;

void postfx_bloom_apply(postfx_bloom_ctx *ctx,
                        uint32_t *pixels, int width, int height,
                        const postfx_bloom *cfg);

/* ===================================================================
 *   Halftone (newspaper / pop-art print)
 * =================================================================== */

/* MONO renders each cell as a single black-ink dot whose radius grows
 * with darkness — bright cells get a small dot, dark cells get a large
 * one that fills the cell.
 *
 * CMYK splits each cell into four sub-dots (one per ink) at small
 * angular offsets. Sub-dot radii come from a CMYK conversion of the
 * cell's average RGB; the overlay subtractively composites against
 * paper (white by default), so overlapping inks produce secondary
 * colours the way real four-colour printing does. */
typedef enum {
    POSTFX_HALFTONE_MONO = 0,
    POSTFX_HALFTONE_CMYK = 1,
} postfx_halftone_mode;

typedef struct postfx_halftone_ctx postfx_halftone_ctx;

postfx_halftone_ctx *postfx_halftone_create(int width, int height);
void                 postfx_halftone_destroy(postfx_halftone_ctx *ctx);

typedef struct {
    int                  enabled;
    postfx_halftone_mode mode;
    int                  cell_size;   /* pixels per cell, clamped to 3..32 */
    postfx_rgb           paper;       /* background; classic = {255,255,255} */
    postfx_rgb           ink;         /* MONO ink colour; classic = {0,0,0} */
} postfx_halftone;

void postfx_halftone_apply(postfx_halftone_ctx *ctx,
                           uint32_t *pixels, int width, int height,
                           const postfx_halftone *cfg);

#endif /* POSTFX_H */
