#include "postfx.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

/* ===================================================================
 *   Edge detection
 * =================================================================== */

/* For each pixel, look at its neighbours: any large disagreement on
 * object id (silhouette), depth (folds), or normal (creases) flags it
 * as an edge and stamps it black. */
void postfx_apply_edges(uint32_t *pixels,
                        const postfx_gbuffer *g,
                        int w, int h,
                        const postfx_edges *e) {
    static const int N4_DX[] = { 1, -1,  0,  0 };
    static const int N4_DY[] = { 0,  0,  1, -1 };
    static const int N8_DX[] = { 1, -1,  0,  0,  1, -1,  1, -1 };
    static const int N8_DY[] = { 0,  0,  1, -1,  1,  1, -1, -1 };
    const int *DX = e->eight_connected ? N8_DX : N4_DX;
    const int *DY = e->eight_connected ? N8_DY : N4_DY;
    int neigh = e->eight_connected ? 8 : 4;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            int is_edge = 0;

            uint32_t self_id = e->use_object_id ? g->object_id[idx] : 0;
            float    self_d  = e->use_depth     ? g->depth[idx]     : 0.0f;
            float    self_nx = e->use_normal    ? g->normal[idx*3+0] : 0.0f;
            float    self_ny = e->use_normal    ? g->normal[idx*3+1] : 0.0f;
            float    self_nz = e->use_normal    ? g->normal[idx*3+2] : 0.0f;

            for (int k = 0; k < neigh && !is_edge; k++) {
                int nx = x + DX[k], ny = y + DY[k];
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                int nidx = ny * w + nx;

                if (e->use_object_id && g->object_id[nidx] != self_id) {
                    is_edge = 1;
                    break;
                }
                if (e->use_depth) {
                    float dd = self_d - g->depth[nidx];
                    if (dd < 0) dd = -dd;
                    /* depth jumps near the camera should be more
                     * sensitive; scale threshold by depth so a 5cm gap
                     * 50m away doesn't outline. */
                    float scale = self_d * 0.05f + 0.05f;
                    if (dd > e->depth_threshold * scale) {
                        is_edge = 1;
                        break;
                    }
                }
                if (e->use_normal) {
                    float dot = self_nx * g->normal[nidx*3+0]
                              + self_ny * g->normal[nidx*3+1]
                              + self_nz * g->normal[nidx*3+2];
                    if (dot < e->normal_threshold) {
                        is_edge = 1;
                        break;
                    }
                }
            }

            if (is_edge) pixels[idx] = 0xFF000000u;
        }
    }
}

/* ===================================================================
 *   Palette table + quantizer
 * =================================================================== */

/* Hex values pulled from lospec.com for canonical palettes; EGA-ish is
 * a hand-rolled tribute used as the original starting point. Quantizer
 * picks nearest in sRGB space, so palettes with carefully balanced
 * values (Sweetie-16, EDG32, AAP-64, Resurrect-64) read better than
 * those with aggressive saturation (NES) at this quantization level. */
static const postfx_rgb PAL_BW2[] = {
    {  0,   0,   0}, {255, 255, 255},
};
static const postfx_rgb PAL_GB4[] = {  /* GameBoy DMG */
    { 15,  56,  15}, { 48,  98,  48}, {139, 172,  15}, {155, 188,  15},
};
static const postfx_rgb PAL_TWILIGHT5[] = {  /* peach/blue dusk */
    {251, 187, 173}, {238, 134, 149}, { 74, 122, 150}, { 51,  63,  88},
    { 41,  40,  49},
};
static const postfx_rgb PAL_SLSO8[] = {  /* warm sunset cyberpunk */
    { 13,  43,  69}, { 32,  60,  86}, { 84,  78, 104}, {141, 105, 122},
    {208, 129,  89}, {255, 170,  94}, {255, 212, 163}, {255, 236, 214},
};
static const postfx_rgb PAL_EGA16[] = {
    {  0,   0,   0}, { 30,  30,  30}, { 90,  90,  90}, {180, 180, 180},
    {255, 255, 255}, {120,  20,  20}, {220,  60,  60}, {255, 160, 100},
    { 60, 110,  40}, {120, 200,  80}, { 40,  80, 160}, {110, 170, 230},
    {200, 200,  60}, {220, 130,  40}, {120,  60, 140}, { 60,  35,  20},
};
static const postfx_rgb PAL_SWEETIE16[] = {  /* GrafxKid */
    { 26,  28,  44}, { 93,  39,  93}, {177,  62,  83}, {239, 125,  87},
    {255, 205, 117}, {167, 240, 112}, { 56, 183, 100}, { 37, 113, 121},
    { 41,  54, 111}, { 59,  93, 201}, { 65, 166, 246}, {115, 239, 247},
    {244, 244, 244}, {148, 176, 194}, { 86, 108, 134}, { 51,  60,  87},
};
static const postfx_rgb PAL_PICO8[] = {
    {  0,   0,   0}, { 29,  43,  83}, {126,  37,  83}, {  0, 135,  81},
    {171,  82,  54}, { 95,  87,  79}, {194, 195, 199}, {255, 241, 232},
    {255,   0,  77}, {255, 163,   0}, {255, 236,  39}, {  0, 228,  54},
    { 41, 173, 255}, {131, 118, 156}, {255, 119, 168}, {255, 204, 170},
};
static const postfx_rgb PAL_VINIK24[] = {  /* desaturated, moody */
    {  0,   0,   0}, {111, 103, 118}, {154, 154, 151}, {197, 204, 184},
    {139,  85, 128}, {195, 136, 144}, {165, 147, 165}, {102,  96, 146},
    {154,  79,  80}, {194, 141, 117}, {124, 161, 192}, { 65, 106, 163},
    {141,  98, 104}, {190, 149,  92}, {104, 172, 169}, { 56, 112, 128},
    {110, 105,  98}, {147, 161, 103}, {110, 170, 120}, { 85, 112, 100},
    {157, 159, 127}, {126, 158, 153}, { 93, 104, 114}, { 67,  52,  85},
};
static const postfx_rgb PAL_EDG32[] = {  /* Endesga 32 — modern indie standard */
    {190,  74,  47}, {215, 118,  67}, {234, 212, 170}, {228, 166, 114},
    {184, 111,  80}, {115,  62,  57}, { 62,  39,  49}, {162,  38,  51},
    {228,  59,  68}, {247, 118,  34}, {254, 174,  52}, {254, 231,  97},
    { 99, 199,  77}, { 62, 137,  72}, { 38,  92,  66}, { 25,  60,  62},
    { 18,  78, 137}, {  0, 153, 219}, { 44, 232, 245}, {255, 255, 255},
    {192, 203, 220}, {139, 155, 180}, { 90, 105, 136}, { 58,  68, 102},
    { 38,  43,  68}, { 24,  20,  37}, {255,   0,  68}, {104,  56, 108},
    {181,  80, 136}, {246, 117, 122}, {232, 183, 150}, {194, 133, 105},
};
static const postfx_rgb PAL_APOLLO[] = {  /* AdamCYounis — naturalistic 46 */
    { 23,  32,  56}, { 37,  58,  94}, { 60,  94, 139}, { 79, 143, 186},
    {115, 190, 211}, {164, 221, 219}, { 25,  51,  45}, { 37,  86,  46},
    { 70, 130,  50}, {117, 167,  67}, {168, 202,  88}, {208, 218, 145},
    { 77,  43,  50}, {122,  72,  65}, {173, 119,  87}, {192, 148, 115},
    {215, 181, 148}, {231, 213, 179}, { 52,  28,  39}, { 96,  44,  44},
    {136,  75,  43}, {190, 119,  43}, {222, 158,  65}, {232, 193, 112},
    { 36,  21,  39}, { 65,  29,  49}, {117,  36,  56}, {165,  48,  48},
    {207,  87,  60}, {218, 134,  62}, { 30,  29,  57}, { 64,  39,  81},
    {122,  54, 123}, {162,  62, 140}, {198,  81, 151}, {223, 132, 165},
    {  9,  10,  20}, { 16,  20,  31}, { 21,  29,  40}, { 32,  46,  55},
    { 57,  74,  80}, { 87, 114, 119}, {129, 151, 150}, {168, 181, 178},
    {199, 207, 204}, {235, 237, 233},
};
static const postfx_rgb PAL_NES55[] = {  /* NES master palette (55 entries) */
    {  0,   0,   0}, {252, 252, 252}, {248, 248, 248}, {188, 188, 188},
    {124, 124, 124}, {164, 228, 252}, { 60, 188, 252}, {  0, 120, 248},
    {  0,   0, 252}, {184, 184, 248}, {104, 136, 252}, {  0,  88, 248},
    {  0,   0, 188}, {216, 184, 248}, {152, 120, 248}, {104,  68, 252},
    { 68,  40, 188}, {248, 184, 248}, {248, 120, 248}, {216,   0, 204},
    {148,   0, 132}, {248, 164, 192}, {248,  88, 152}, {228,   0,  88},
    {168,   0,  32}, {240, 208, 176}, {248, 120,  88}, {248,  56,   0},
    {168,  16,   0}, {252, 224, 168}, {252, 160,  68}, {228,  92,  16},
    {136,  20,   0}, {248, 216, 120}, {248, 184,   0}, {172, 124,   0},
    { 80,  48,   0}, {216, 248, 120}, {184, 248,  24}, {  0, 184,   0},
    {  0, 120,   0}, {184, 248, 184}, { 88, 216,  84}, {  0, 168,   0},
    {  0, 104,   0}, {184, 248, 216}, { 88, 248, 152}, {  0, 168,  68},
    {  0,  88,   0}, {  0, 252, 252}, {  0, 232, 216}, {  0, 136, 136},
    {  0,  64,  88}, {248, 216, 248}, {120, 120, 120},
};
static const postfx_rgb PAL_AAP64[] = {  /* Adigun A. Polack — high-richness 64 */
    {  6,   6,   8}, { 20,  16,  19}, { 59,  23,  37}, {115,  23,  45},
    {180,  32,  42}, {223,  62,  35}, {250, 106,  10}, {249, 163,  27},
    {255, 213,  65}, {255, 252,  64}, {214, 242, 100}, {156, 219,  67},
    { 89, 193,  53}, { 20, 160,  46}, { 26, 122,  62}, { 36,  82,  59},
    { 18,  32,  32}, { 20,  52, 100}, { 40,  92, 196}, { 36, 159, 222},
    { 32, 214, 199}, {166, 252, 219}, {255, 255, 255}, {254, 243, 192},
    {250, 214, 184}, {245, 160, 151}, {232, 106, 115}, {188,  74, 155},
    {121,  58, 128}, { 64,  51,  83}, { 36,  34,  52}, { 34,  28,  26},
    { 50,  43,  40}, {113,  65,  59}, {187, 117,  71}, {219, 164,  99},
    {244, 210, 156}, {218, 224, 234}, {179, 185, 209}, {139, 147, 175},
    {109, 117, 141}, { 74,  84,  98}, { 51,  57,  65}, { 66,  36,  51},
    { 91,  49,  56}, {142,  82,  82}, {186, 117, 106}, {233, 181, 163},
    {227, 230, 255}, {185, 191, 251}, {132, 155, 228}, { 88, 141, 190},
    { 71, 125, 133}, { 35, 103,  78}, { 50, 132, 100}, { 93, 175, 141},
    {146, 220, 186}, {205, 247, 226}, {228, 210, 170}, {199, 176, 139},
    {160, 134,  98}, {121, 103,  85}, { 90,  78,  68}, { 66,  57,  52},
};
static const postfx_rgb PAL_RESURRECT64[] = {  /* Kerrie Lake — cooler 64 */
    { 46,  34,  47}, { 62,  53,  70}, { 98,  85, 101}, {150, 108, 108},
    {171, 148, 122}, {105,  79,  98}, {127, 112, 138}, {155, 171, 178},
    {199, 220, 208}, {255, 255, 255}, {110,  39,  39}, {179,  56,  49},
    {234,  79,  54}, {245, 125,  74}, {174,  35,  52}, {232,  59,  59},
    {251, 107,  29}, {247, 150,  23}, {249, 194,  43}, {122,  48,  69},
    {158,  69,  57}, {205, 104,  61}, {230, 144,  78}, {251, 185,  84},
    { 76,  62,  36}, {103, 102,  51}, {162, 169,  71}, {213, 224,  75},
    {251, 255, 134}, { 22,  90,  76}, { 35, 144,  99}, { 30, 188, 115},
    {145, 219, 105}, {205, 223, 108}, { 49,  54,  56}, { 55,  78,  74},
    { 84, 126, 100}, {146, 169, 132}, {178, 186, 144}, { 11,  94, 101},
    { 11, 138, 143}, { 14, 175, 155}, { 48, 225, 185}, {143, 248, 226},
    { 50,  51,  83}, { 72,  74, 119}, { 77, 101, 180}, { 77, 155, 230},
    {143, 211, 255}, { 69,  41,  63}, {107,  62, 117}, {144,  94, 169},
    {168, 132, 243}, {234, 173, 237}, {117,  60,  84}, {162,  75, 111},
    {207, 101, 127}, {237, 128, 153}, {131,  28,  93}, {195,  36,  84},
    {240,  79, 120}, {246, 129, 129}, {252, 167, 144}, {253, 203, 176},
};

#define PAL_LEN(arr) ((int)(sizeof(arr) / sizeof((arr)[0])))

static const postfx_palette PALETTES[] = {
    { PAL_BW2,         PAL_LEN(PAL_BW2),         "bw2"         },
    { PAL_GB4,         PAL_LEN(PAL_GB4),         "gb4"         },
    { PAL_TWILIGHT5,   PAL_LEN(PAL_TWILIGHT5),   "twilight5"   },
    { PAL_SLSO8,       PAL_LEN(PAL_SLSO8),       "slso8"       },
    { PAL_EGA16,       PAL_LEN(PAL_EGA16),       "ega16"       },
    { PAL_SWEETIE16,   PAL_LEN(PAL_SWEETIE16),   "sweetie16"   },
    { PAL_PICO8,       PAL_LEN(PAL_PICO8),       "pico8"       },
    { PAL_VINIK24,     PAL_LEN(PAL_VINIK24),     "vinik24"     },
    { PAL_EDG32,       PAL_LEN(PAL_EDG32),       "edg32"       },
    { PAL_APOLLO,      PAL_LEN(PAL_APOLLO),      "apollo"      },
    { PAL_NES55,       PAL_LEN(PAL_NES55),       "nes55"       },
    { PAL_AAP64,       PAL_LEN(PAL_AAP64),       "aap64"       },
    { PAL_RESURRECT64, PAL_LEN(PAL_RESURRECT64), "resurrect64" },
};
#define PALETTE_COUNT ((int)(sizeof(PALETTES) / sizeof(PALETTES[0])))

const postfx_palette *postfx_palette_at(int index) {
    if (index < 0 || index >= PALETTE_COUNT) return NULL;
    return &PALETTES[index];
}

int postfx_palette_count(void) {
    return PALETTE_COUNT;
}

/* 4x4 Bayer matrix; multiply by DITHER_SPREAD to shift each channel
 * before nearest-palette lookup. */
static const int BAYER4[16] = {
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5,
};
#define DITHER_SPREAD 28  /* tuned by eye against the EGA/PICO-8 palettes */

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint32_t palette_nearest(const postfx_palette *p,
                                       int r, int g, int b) {
    int best = 0;
    int best_d = 1 << 30;
    for (int i = 0; i < p->count; i++) {
        int dr = r - p->colors[i].r;
        int dg = g - p->colors[i].g;
        int db = b - p->colors[i].b;
        int d  = dr*dr + dg*dg + db*db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return 0xFF000000u
         | ((uint32_t)p->colors[best].r << 16)
         | ((uint32_t)p->colors[best].g <<  8)
         |  (uint32_t)p->colors[best].b;
}

void postfx_quantize(uint32_t *pixels, int w, int h,
                     const postfx_palette *p, int dither) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t c = pixels[y * w + x];
            int r = (c >> 16) & 0xFF;
            int g = (c >>  8) & 0xFF;
            int b =  c        & 0xFF;
            if (dither) {
                int t = BAYER4[(y & 3) * 4 + (x & 3)];
                int off = ((t * 2 - 15) * DITHER_SPREAD) / 15;  /* [-spread, +spread] */
                r = clampi(r + off, 0, 255);
                g = clampi(g + off, 0, 255);
                b = clampi(b + off, 0, 255);
            }
            pixels[y * w + x] = palette_nearest(p, r, g, b);
        }
    }
}

/* ===================================================================
 *   Posterize
 * =================================================================== */

/* Snap luminance to N bands while preserving chroma — a cheap stand-in
 * for true cel shading. Scaling each channel by snapped/actual lum
 * keeps hue stable while banding the brightness. */
#define POSTERIZE_BANDS 4

static inline uint32_t posterize_pixel(uint32_t argb) {
    int r = (argb >> 16) & 0xFF;
    int g = (argb >>  8) & 0xFF;
    int b =  argb        & 0xFF;
    int lum = (77 * r + 150 * g + 29 * b) >> 8;  /* Rec.601, fixed-point */
    if (lum <= 0) return argb;
    int band = (lum * POSTERIZE_BANDS) / 256;     /* 0..BANDS-1 */
    int snap = ((band + 1) * 255) / POSTERIZE_BANDS;
    int nr = clampi((r * snap) / lum, 0, 255);
    int ng = clampi((g * snap) / lum, 0, 255);
    int nb = clampi((b * snap) / lum, 0, 255);
    return 0xFF000000u | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
}

void postfx_posterize(uint32_t *pixels, int w, int h) {
    int n = w * h;
    for (int i = 0; i < n; i++) pixels[i] = posterize_pixel(pixels[i]);
}

/* ===================================================================
 *   Bloom
 * =================================================================== */

#define BLOOM_MAX_RADIUS 16

struct postfx_bloom_ctx {
    int    input_w, input_h;
    int    low_w, low_h;
    float *bright_a;   /* low_w * low_h * 3, ping-pong A */
    float *bright_b;   /* low_w * low_h * 3, ping-pong B */
};

static int bloom_realloc(postfx_bloom_ctx *c, int w, int h) {
    int lw = w >> 1; if (lw < 1) lw = 1;
    int lh = h >> 1; if (lh < 1) lh = 1;
    size_t n = (size_t)lw * (size_t)lh * 3;
    float *a = calloc(n, sizeof(float));
    float *b = calloc(n, sizeof(float));
    if (!a || !b) { free(a); free(b); return 0; }
    free(c->bright_a);
    free(c->bright_b);
    c->bright_a = a;
    c->bright_b = b;
    c->input_w  = w;  c->input_h = h;
    c->low_w    = lw; c->low_h   = lh;
    return 1;
}

postfx_bloom_ctx *postfx_bloom_create(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    postfx_bloom_ctx *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (!bloom_realloc(c, w, h)) { free(c); return NULL; }
    return c;
}

void postfx_bloom_destroy(postfx_bloom_ctx *c) {
    if (!c) return;
    free(c->bright_a);
    free(c->bright_b);
    free(c);
}

/* Soft-knee bright pass: pixels well below threshold contribute zero,
 * those near it ramp in smoothly, those above pass their excess through.
 * Output is scaled-down RGB in [0, ~1] floats. */
static inline void bright_pass_pixel(float r, float g, float b,
                                     float thr, float knee,
                                     float *out_r, float *out_g, float *out_b) {
    float lum = 0.299f * r + 0.587f * g + 0.114f * b;
    float soft = lum - thr + knee;
    if (soft < 0) soft = 0;
    if (soft > 2.0f * knee) soft = 2.0f * knee;
    soft = (soft * soft) / (4.0f * knee + 1e-6f);
    float hard = lum - thr;
    if (hard < 0) hard = 0;
    float contrib = soft > hard ? soft : hard;
    float mul = lum > 1e-6f ? contrib / lum : 0.0f;
    *out_r = r * mul;
    *out_g = g * mul;
    *out_b = b * mul;
}

void postfx_bloom_apply(postfx_bloom_ctx *c, uint32_t *pixels,
                        int w, int h, const postfx_bloom *cfg) {
    if (!c || !cfg || !cfg->enabled) return;
    if (w != c->input_w || h != c->input_h) {
        if (!bloom_realloc(c, w, h)) return;
    }
    int lw = c->low_w, lh = c->low_h;
    float knee = cfg->knee > 1e-3f ? cfg->knee : 1e-3f;
    float thr  = cfg->threshold;

    /* 1. Bright pass + 2x downsample (average a 2x2 block). */
    for (int y = 0; y < lh; y++) {
        int sy0 = y * 2;
        int sy1 = sy0 + 1; if (sy1 >= h) sy1 = sy0;
        for (int x = 0; x < lw; x++) {
            int sx0 = x * 2;
            int sx1 = sx0 + 1; if (sx1 >= w) sx1 = sx0;
            uint32_t p00 = pixels[sy0 * w + sx0];
            uint32_t p10 = pixels[sy0 * w + sx1];
            uint32_t p01 = pixels[sy1 * w + sx0];
            uint32_t p11 = pixels[sy1 * w + sx1];
            float r = (((p00>>16)&0xFF) + ((p10>>16)&0xFF) + ((p01>>16)&0xFF) + ((p11>>16)&0xFF)) * (1.0f / (4.0f * 255.0f));
            float g = (((p00>> 8)&0xFF) + ((p10>> 8)&0xFF) + ((p01>> 8)&0xFF) + ((p11>> 8)&0xFF)) * (1.0f / (4.0f * 255.0f));
            float b = (( p00     &0xFF) + ( p10     &0xFF) + ( p01     &0xFF) + ( p11     &0xFF)) * (1.0f / (4.0f * 255.0f));
            float br, bg, bb;
            bright_pass_pixel(r, g, b, thr, knee, &br, &bg, &bb);
            float *dst = &c->bright_a[(y * lw + x) * 3];
            dst[0] = br; dst[1] = bg; dst[2] = bb;
        }
    }

    /* 2. Build the 1D Gaussian kernel once. */
    int radius = cfg->radius;
    if (radius < 1) radius = 1;
    if (radius > BLOOM_MAX_RADIUS) radius = BLOOM_MAX_RADIUS;
    float kernel[2 * BLOOM_MAX_RADIUS + 1];
    float sigma = (float)radius * 0.5f;
    float ksum = 0.0f;
    for (int i = -radius; i <= radius; i++) {
        float v = expf(-((float)(i * i)) / (2.0f * sigma * sigma));
        kernel[i + radius] = v;
        ksum += v;
    }
    float inv_ksum = 1.0f / ksum;
    for (int i = 0; i < 2 * radius + 1; i++) kernel[i] *= inv_ksum;

    int iters = cfg->iterations;
    if (iters < 1) iters = 1;
    if (iters > 4) iters = 4;

    /* 3. Separable Gaussian blur, ping-ponging A↔B. */
    for (int it = 0; it < iters; it++) {
        /* Horizontal: A → B. */
        for (int y = 0; y < lh; y++) {
            const float *row = &c->bright_a[y * lw * 3];
            float *out = &c->bright_b[y * lw * 3];
            for (int x = 0; x < lw; x++) {
                float r = 0, g = 0, b = 0;
                for (int k = -radius; k <= radius; k++) {
                    int xx = x + k;
                    if (xx < 0)   xx = 0;
                    if (xx >= lw) xx = lw - 1;
                    float wk = kernel[k + radius];
                    const float *p = &row[xx * 3];
                    r += wk * p[0];
                    g += wk * p[1];
                    b += wk * p[2];
                }
                out[x * 3 + 0] = r;
                out[x * 3 + 1] = g;
                out[x * 3 + 2] = b;
            }
        }
        /* Vertical: B → A. */
        for (int y = 0; y < lh; y++) {
            for (int x = 0; x < lw; x++) {
                float r = 0, g = 0, b = 0;
                for (int k = -radius; k <= radius; k++) {
                    int yy = y + k;
                    if (yy < 0)   yy = 0;
                    if (yy >= lh) yy = lh - 1;
                    float wk = kernel[k + radius];
                    const float *p = &c->bright_b[(yy * lw + x) * 3];
                    r += wk * p[0];
                    g += wk * p[1];
                    b += wk * p[2];
                }
                float *out = &c->bright_a[(y * lw + x) * 3];
                out[0] = r;
                out[1] = g;
                out[2] = b;
            }
        }
    }

    /* 4. Bilinear upsample bright_a and add additively to pixels.
     * Source coord maps the half-res texel centre to its full-res
     * counterpart; clamp on the edge (no wrap). */
    float intensity = cfg->intensity;
    for (int y = 0; y < h; y++) {
        float fy = ((float)y + 0.5f) * 0.5f - 0.5f;
        int   y0 = (int)floorf(fy);
        float ty = fy - (float)y0;
        int   y1 = y0 + 1;
        if (y0 < 0)  { y0 = 0; ty = 0.0f; }
        if (y1 >= lh) y1 = lh - 1;
        for (int x = 0; x < w; x++) {
            float fx = ((float)x + 0.5f) * 0.5f - 0.5f;
            int   x0 = (int)floorf(fx);
            float tx = fx - (float)x0;
            int   x1 = x0 + 1;
            if (x0 < 0)  { x0 = 0; tx = 0.0f; }
            if (x1 >= lw) x1 = lw - 1;

            float w00 = (1.0f - tx) * (1.0f - ty);
            float w10 = tx          * (1.0f - ty);
            float w01 = (1.0f - tx) * ty;
            float w11 = tx          * ty;

            const float *p00 = &c->bright_a[(y0 * lw + x0) * 3];
            const float *p10 = &c->bright_a[(y0 * lw + x1) * 3];
            const float *p01 = &c->bright_a[(y1 * lw + x0) * 3];
            const float *p11 = &c->bright_a[(y1 * lw + x1) * 3];

            float br = w00 * p00[0] + w10 * p10[0] + w01 * p01[0] + w11 * p11[0];
            float bg = w00 * p00[1] + w10 * p10[1] + w01 * p01[1] + w11 * p11[1];
            float bb = w00 * p00[2] + w10 * p10[2] + w01 * p01[2] + w11 * p11[2];

            uint32_t orig = pixels[y * w + x];
            int ar = ((orig >> 16) & 0xFF) + (int)(intensity * br * 255.0f);
            int ag = ((orig >>  8) & 0xFF) + (int)(intensity * bg * 255.0f);
            int ab = ( orig        & 0xFF) + (int)(intensity * bb * 255.0f);
            pixels[y * w + x] = 0xFF000000u
                              | ((uint32_t)clampi(ar, 0, 255) << 16)
                              | ((uint32_t)clampi(ag, 0, 255) <<  8)
                              |  (uint32_t)clampi(ab, 0, 255);
        }
    }
}

/* ===================================================================
 *   Halftone
 * =================================================================== */

#define HALFTONE_MIN_CELL 3
#define HALFTONE_MAX_CELL 32

struct postfx_halftone_ctx {
    int    input_w, input_h;
    int    cell_size;
    int    cells_x, cells_y;
    /* Per-cell averaged channels: r, g, b, count (count-as-float so the
     * normalize step is one divide). */
    float *cells;     /* cells_x * cells_y * 4 floats */
};

static int halftone_realloc(postfx_halftone_ctx *c, int w, int h, int csz) {
    int cx = (w + csz - 1) / csz;
    int cy = (h + csz - 1) / csz;
    size_t n = (size_t)cx * (size_t)cy * 4;
    float *cells = calloc(n, sizeof(float));
    if (!cells) return 0;
    free(c->cells);
    c->cells     = cells;
    c->input_w   = w;  c->input_h  = h;
    c->cell_size = csz;
    c->cells_x   = cx; c->cells_y  = cy;
    return 1;
}

postfx_halftone_ctx *postfx_halftone_create(int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    postfx_halftone_ctx *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (!halftone_realloc(c, w, h, 8)) { free(c); return NULL; }
    return c;
}

void postfx_halftone_destroy(postfx_halftone_ctx *c) {
    if (!c) return;
    free(c->cells);
    free(c);
}

/* CMYK from sRGB (no gamma — same approximation we use for the bright
 * pass; good enough for the stylized look). K = 1 - max(R,G,B); the
 * remaining channels are the pure-CMY component left after K. */
static inline void rgb_to_cmyk(float r, float g, float b,
                               float *c, float *m, float *y, float *k) {
    float mx = r > g ? r : g;
    if (b > mx) mx = b;
    float kk = 1.0f - mx;
    if (kk >= 1.0f - 1e-6f) {
        *c = *m = *y = 0.0f;
        *k = 1.0f;
        return;
    }
    float inv = 1.0f / (1.0f - kk);
    *c = (1.0f - r - kk) * inv;
    *m = (1.0f - g - kk) * inv;
    *y = (1.0f - b - kk) * inv;
    *k = kk;
}

void postfx_halftone_apply(postfx_halftone_ctx *c, uint32_t *pixels,
                           int w, int h, const postfx_halftone *cfg) {
    if (!c || !cfg || !cfg->enabled) return;
    int csz = cfg->cell_size;
    if (csz < HALFTONE_MIN_CELL) csz = HALFTONE_MIN_CELL;
    if (csz > HALFTONE_MAX_CELL) csz = HALFTONE_MAX_CELL;

    if (w != c->input_w || h != c->input_h || csz != c->cell_size) {
        if (!halftone_realloc(c, w, h, csz)) return;
    }
    int cx = c->cells_x, cy = c->cells_y;

    /* 1. Aggregate per-cell channel sums in one sweep through the
     * input. Cells along the right/bottom edge may be partially
     * filled, hence the per-cell count. */
    size_t cells_n = (size_t)cx * (size_t)cy * 4;
    for (size_t i = 0; i < cells_n; i++) c->cells[i] = 0.0f;

    for (int y = 0; y < h; y++) {
        int gy = y / csz; if (gy >= cy) gy = cy - 1;
        for (int x = 0; x < w; x++) {
            int gx = x / csz; if (gx >= cx) gx = cx - 1;
            uint32_t p = pixels[y * w + x];
            float *cell = &c->cells[(gy * cx + gx) * 4];
            cell[0] += (float)((p >> 16) & 0xFF);
            cell[1] += (float)((p >>  8) & 0xFF);
            cell[2] += (float)( p        & 0xFF);
            cell[3] += 1.0f;
        }
    }
    for (int i = 0; i < cx * cy; i++) {
        float n = c->cells[i * 4 + 3];
        if (n > 0.0f) {
            float inv = 1.0f / (n * 255.0f);
            c->cells[i * 4 + 0] *= inv;
            c->cells[i * 4 + 1] *= inv;
            c->cells[i * 4 + 2] *= inv;
        }
    }

    /* 2. Rebuild every pixel as either paper or one-or-more inks,
     * depending on the dot pattern at its cell. Max dot radius equals
     * the cell half-diagonal so a fully-saturated dot fully covers
     * its cell. */
    float max_r = (float)csz * 0.5f * 1.4142135f;

    /* CMYK sub-dot offsets in cell-local coords. The four offsets are
     * arranged in a square inside the cell so each ink's screen sits
     * on a slightly different lattice — kills moire and recreates the
     * registration look of real four-colour printing. */
    float off = (float)csz * 0.18f;
    float cmyk_off_x[4] = { -off, +off, +off, -off };  /* C, M, Y, K */
    float cmyk_off_y[4] = { -off, -off, +off, +off };
    /* CMYK sub-dot radius cap: each ink only gets ~half the cell so
     * overlapping inks have room to read. */
    float cmyk_max_r = max_r * 0.55f;

    int paper_r = cfg->paper.r, paper_g = cfg->paper.g, paper_b = cfg->paper.b;
    int ink_r   = cfg->ink.r,   ink_g   = cfg->ink.g,   ink_b   = cfg->ink.b;

    for (int y = 0; y < h; y++) {
        int gy = y / csz; if (gy >= cy) gy = cy - 1;
        int center_y = gy * csz + csz / 2;
        for (int x = 0; x < w; x++) {
            int gx = x / csz; if (gx >= cx) gx = cx - 1;
            int center_x = gx * csz + csz / 2;
            const float *cell = &c->cells[(gy * cx + gx) * 4];
            float r = cell[0], g = cell[1], b = cell[2];

            if (cfg->mode == POSTFX_HALFTONE_MONO) {
                float lum = 0.299f * r + 0.587f * g + 0.114f * b;
                float dot_r = (1.0f - lum) * max_r;
                float dx = (float)(x - center_x);
                float dy = (float)(y - center_y);
                int inside = (dx * dx + dy * dy) < (dot_r * dot_r);
                if (inside) {
                    pixels[y * w + x] = 0xFF000000u
                                      | ((uint32_t)ink_r << 16)
                                      | ((uint32_t)ink_g <<  8)
                                      |  (uint32_t)ink_b;
                } else {
                    pixels[y * w + x] = 0xFF000000u
                                      | ((uint32_t)paper_r << 16)
                                      | ((uint32_t)paper_g <<  8)
                                      |  (uint32_t)paper_b;
                }
            } else {
                /* CMYK: start with paper, then for each ink whose
                 * sub-dot covers this pixel, subtractively absorb the
                 * matching channel. */
                float cyan, magenta, yellow, black;
                rgb_to_cmyk(r, g, b, &cyan, &magenta, &yellow, &black);
                float ink_strength[4] = { cyan, magenta, yellow, black };

                float pr = paper_r / 255.0f;
                float pg = paper_g / 255.0f;
                float pb = paper_b / 255.0f;

                for (int k = 0; k < 4; k++) {
                    float dx = (float)x - ((float)center_x + cmyk_off_x[k]);
                    float dy = (float)y - ((float)center_y + cmyk_off_y[k]);
                    float dot_r = ink_strength[k] * cmyk_max_r;
                    if (dx * dx + dy * dy >= dot_r * dot_r) continue;
                    /* Subtractive: each ink kills the channels it
                     * absorbs. */
                    switch (k) {
                        case 0: pr = 0.0f; break;                       /* cyan    */
                        case 1: pg = 0.0f; break;                       /* magenta */
                        case 2: pb = 0.0f; break;                       /* yellow  */
                        case 3: pr = pg = pb = 0.0f; break;             /* black   */
                    }
                }
                int ar = (int)(pr * 255.0f + 0.5f);
                int ag = (int)(pg * 255.0f + 0.5f);
                int ab = (int)(pb * 255.0f + 0.5f);
                pixels[y * w + x] = 0xFF000000u
                                  | ((uint32_t)clampi(ar, 0, 255) << 16)
                                  | ((uint32_t)clampi(ag, 0, 255) <<  8)
                                  |  (uint32_t)clampi(ab, 0, 255);
            }
        }
    }
}

/* ===================================================================
 *   Toon
 * =================================================================== */

void postfx_toon_apply(uint32_t *pixels,
                       const postfx_gbuffer *g,
                       int w, int h,
                       const postfx_toon *cfg) {
    if (!cfg || !cfg->enabled || !g || !g->normal) return;
    int bands = cfg->bands;
    if (bands < 2) bands = 2;
    if (bands > 6) bands = 6;

    /* Normalise the light direction once. Caller can pass any
     * non-zero vector. */
    float lx = cfg->light_x, ly = cfg->light_y, lz = cfg->light_z;
    float llen = sqrtf(lx * lx + ly * ly + lz * lz);
    if (llen < 1e-6f) return;
    float inv_l = 1.0f / llen;
    lx *= inv_l; ly *= inv_l; lz *= inv_l;

    float ambient = cfg->ambient;
    if (ambient < 0.0f) ambient = 0.0f;
    if (ambient > 1.0f) ambient = 1.0f;
    float rim = cfg->rim_strength;
    if (rim < 0.0f) rim = 0.0f;

    /* Per-band brightness: band 0 = ambient, band (bands-1) = 1.0,
     * with linear steps in between. */
    float band_step = (1.0f - ambient) / (float)(bands - 1);

    int n = w * h;
    for (int i = 0; i < n; i++) {
        float nx = g->normal[i * 3 + 0];
        float ny = g->normal[i * 3 + 1];
        float nz = g->normal[i * 3 + 2];
        /* Background (no geometry hit) has zero-length normal; skip
         * those pixels so the sky stays at full brightness. */
        float nlen2 = nx * nx + ny * ny + nz * nz;
        if (nlen2 < 1e-4f) continue;

        float ndl = nx * lx + ny * ly + nz * lz;
        if (ndl < 0.0f) ndl = 0.0f;

        /* Quantize n·l to a band index in [0, bands-1]. Multiplying
         * by `bands` and flooring gives bands+1 buckets (0..bands);
         * clamping to bands-1 collapses the top sliver into the
         * brightest band. */
        int   band_idx = (int)(ndl * (float)bands);
        if (band_idx >= bands) band_idx = bands - 1;
        float mult = ambient + (float)band_idx * band_step;

        /* Rim: brightens pixels whose normal faces away from the
         * light, e.g. silhouettes in profile. Adds a soft fringe
         * that reads like backlight. */
        if (rim > 0.0f) {
            float rim_t = 1.0f - ndl;
            rim_t *= rim_t;  /* sharpen the edge */
            mult += rim * rim_t;
        }
        if (mult > 1.5f) mult = 1.5f;

        uint32_t p = pixels[i];
        int r = (int)(((p >> 16) & 0xFF) * mult + 0.5f);
        int gC = (int)(((p >>  8) & 0xFF) * mult + 0.5f);
        int b = (int)(( p        & 0xFF) * mult + 0.5f);
        pixels[i] = 0xFF000000u
                  | ((uint32_t)clampi(r,  0, 255) << 16)
                  | ((uint32_t)clampi(gC, 0, 255) <<  8)
                  |  (uint32_t)clampi(b,  0, 255);
    }
}
