#include "postfx.h"
#include <stddef.h>
#include <stdint.h>

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
