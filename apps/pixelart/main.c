/* Pixel-art raytrace demo.
 *
 * Renders the scene at a fixed low internal resolution and lets the
 * GL_NEAREST blit do the upscale, so every traced pixel becomes one
 * chunky on-screen pixel. Optional palette quantization snaps the
 * output to a small fixed palette for an even more deliberate look.
 *
 * Controls:
 *   ESC       quit
 *   TAB       toggle CPU / OpenGL backend (if both available)
 *   1..4      resolution preset: 160x90 / 240x135 / 320x180 / 480x270
 *   P         toggle palette quantization
 *   [ / ]     cycle palette (bw2, gb4, twilight5, slso8, ega16, sweetie16,
 *             pico8, vinik24, edg32 [default], apollo, nes55, aap64,
 *             resurrect64 — 13 palettes total, low to high colour count)
 *   H         toggle ordered dither (4x4 Bayer)
 *   O         toggle luminance posterize (cel-shading approximation)
 *   F11       fullscreen
 *   WASD/space/shift  fly camera; arrows look around
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include "plane.h"
#include "box.h"
#include "cylinder.h"
#include "obj.h"
#include "mesh.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 540
#define FOV (M_PI / 3.0f)

/* Resolution presets. Order matters: keys 1..N pick presets[i]. */
typedef struct { int w, h; const char *name; } pixel_preset;
static const pixel_preset PRESETS[] = {
    { 160,  90, "160x90"  },
    { 240, 135, "240x135" },
    { 320, 180, "320x180" },
    { 480, 270, "480x270" },
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
#define PRESET_DEFAULT 1  /* 240x135 */

/* Curated palettes. The quantizer just picks nearest in sRGB-space —
 * crude but fine for a prototype. PICO-8 and GameBoy values are the
 * canonical published ones. EGA-ish is hand-rolled for the test scene. */
typedef struct { uint8_t r, g, b; } rgb;
typedef struct { const rgb *colors; int count; const char *name; } palette;

/* Hex values pulled from lospec.com for canonical palettes; EGA-ish is
 * a hand-rolled tribute used as the original starting point. Quantizer
 * picks nearest in sRGB space, so palettes with carefully balanced
 * values (Sweetie-16, EDG32, AAP-64, Resurrect-64) read better than
 * those with aggressive saturation (NES) at this quantization level. */
static const rgb PAL_BW2[] = {
    {  0,   0,   0}, {255, 255, 255},
};
static const rgb PAL_GB4[] = {  /* GameBoy DMG */
    { 15,  56,  15}, { 48,  98,  48}, {139, 172,  15}, {155, 188,  15},
};
static const rgb PAL_TWILIGHT5[] = {  /* peach/blue dusk */
    {251, 187, 173}, {238, 134, 149}, { 74, 122, 150}, { 51,  63,  88},
    { 41,  40,  49},
};
static const rgb PAL_SLSO8[] = {  /* warm sunset cyberpunk */
    { 13,  43,  69}, { 32,  60,  86}, { 84,  78, 104}, {141, 105, 122},
    {208, 129,  89}, {255, 170,  94}, {255, 212, 163}, {255, 236, 214},
};
static const rgb PAL_EGA16[] = {
    {  0,   0,   0}, { 30,  30,  30}, { 90,  90,  90}, {180, 180, 180},
    {255, 255, 255}, {120,  20,  20}, {220,  60,  60}, {255, 160, 100},
    { 60, 110,  40}, {120, 200,  80}, { 40,  80, 160}, {110, 170, 230},
    {200, 200,  60}, {220, 130,  40}, {120,  60, 140}, { 60,  35,  20},
};
static const rgb PAL_SWEETIE16[] = {  /* GrafxKid */
    { 26,  28,  44}, { 93,  39,  93}, {177,  62,  83}, {239, 125,  87},
    {255, 205, 117}, {167, 240, 112}, { 56, 183, 100}, { 37, 113, 121},
    { 41,  54, 111}, { 59,  93, 201}, { 65, 166, 246}, {115, 239, 247},
    {244, 244, 244}, {148, 176, 194}, { 86, 108, 134}, { 51,  60,  87},
};
static const rgb PAL_PICO8[] = {
    {  0,   0,   0}, { 29,  43,  83}, {126,  37,  83}, {  0, 135,  81},
    {171,  82,  54}, { 95,  87,  79}, {194, 195, 199}, {255, 241, 232},
    {255,   0,  77}, {255, 163,   0}, {255, 236,  39}, {  0, 228,  54},
    { 41, 173, 255}, {131, 118, 156}, {255, 119, 168}, {255, 204, 170},
};
static const rgb PAL_VINIK24[] = {  /* desaturated, moody */
    {  0,   0,   0}, {111, 103, 118}, {154, 154, 151}, {197, 204, 184},
    {139,  85, 128}, {195, 136, 144}, {165, 147, 165}, {102,  96, 146},
    {154,  79,  80}, {194, 141, 117}, {124, 161, 192}, { 65, 106, 163},
    {141,  98, 104}, {190, 149,  92}, {104, 172, 169}, { 56, 112, 128},
    {110, 105,  98}, {147, 161, 103}, {110, 170, 120}, { 85, 112, 100},
    {157, 159, 127}, {126, 158, 153}, { 93, 104, 114}, { 67,  52,  85},
};
static const rgb PAL_EDG32[] = {  /* Endesga 32 — modern indie standard */
    {190,  74,  47}, {215, 118,  67}, {234, 212, 170}, {228, 166, 114},
    {184, 111,  80}, {115,  62,  57}, { 62,  39,  49}, {162,  38,  51},
    {228,  59,  68}, {247, 118,  34}, {254, 174,  52}, {254, 231,  97},
    { 99, 199,  77}, { 62, 137,  72}, { 38,  92,  66}, { 25,  60,  62},
    { 18,  78, 137}, {  0, 153, 219}, { 44, 232, 245}, {255, 255, 255},
    {192, 203, 220}, {139, 155, 180}, { 90, 105, 136}, { 58,  68, 102},
    { 38,  43,  68}, { 24,  20,  37}, {255,   0,  68}, {104,  56, 108},
    {181,  80, 136}, {246, 117, 122}, {232, 183, 150}, {194, 133, 105},
};
static const rgb PAL_APOLLO[] = {  /* AdamCYounis — naturalistic 46 */
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
static const rgb PAL_NES55[] = {  /* NES master palette (55 entries) */
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
static const rgb PAL_AAP64[] = {  /* Adigun A. Polack — high-richness 64 */
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
static const rgb PAL_RESURRECT64[] = {  /* Kerrie Lake — cooler 64 */
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

/* Ordered low → high colour count. EDG-32 is the default — pleasant
 * mid-density that flatters the test scene. Cycle [/] in either
 * direction to taste. */
static const palette PALETTES[] = {
    { PAL_BW2,         (int)(sizeof(PAL_BW2)        /sizeof(PAL_BW2[0])),         "bw2"         },
    { PAL_GB4,         (int)(sizeof(PAL_GB4)        /sizeof(PAL_GB4[0])),         "gb4"         },
    { PAL_TWILIGHT5,   (int)(sizeof(PAL_TWILIGHT5)  /sizeof(PAL_TWILIGHT5[0])),   "twilight5"   },
    { PAL_SLSO8,       (int)(sizeof(PAL_SLSO8)      /sizeof(PAL_SLSO8[0])),       "slso8"       },
    { PAL_EGA16,       (int)(sizeof(PAL_EGA16)      /sizeof(PAL_EGA16[0])),       "ega16"       },
    { PAL_SWEETIE16,   (int)(sizeof(PAL_SWEETIE16)  /sizeof(PAL_SWEETIE16[0])),   "sweetie16"   },
    { PAL_PICO8,       (int)(sizeof(PAL_PICO8)      /sizeof(PAL_PICO8[0])),       "pico8"       },
    { PAL_VINIK24,     (int)(sizeof(PAL_VINIK24)    /sizeof(PAL_VINIK24[0])),     "vinik24"     },
    { PAL_EDG32,       (int)(sizeof(PAL_EDG32)      /sizeof(PAL_EDG32[0])),       "edg32"       },
    { PAL_APOLLO,      (int)(sizeof(PAL_APOLLO)     /sizeof(PAL_APOLLO[0])),      "apollo"      },
    { PAL_NES55,       (int)(sizeof(PAL_NES55)      /sizeof(PAL_NES55[0])),       "nes55"       },
    { PAL_AAP64,       (int)(sizeof(PAL_AAP64)      /sizeof(PAL_AAP64[0])),       "aap64"       },
    { PAL_RESURRECT64, (int)(sizeof(PAL_RESURRECT64)/sizeof(PAL_RESURRECT64[0])), "resurrect64" },
};
#define PALETTE_DEFAULT 8  /* edg32 */
#define PALETTE_COUNT ((int)(sizeof(PALETTES) / sizeof(PALETTES[0])))

/* 4x4 Bayer matrix scaled to [-7.5, +7.5]; multiply by DITHER_SPREAD to
 * shift each channel before nearest-palette lookup. The threshold
 * ordering is the standard recursive Bayer pattern. */
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

static inline uint32_t palette_nearest(const palette *p, int r, int g, int b) {
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

/* Snap luminance to N bands while preserving chroma — a cheap stand-in
 * for true cel shading. We scale each channel by the ratio of snapped
 * to actual luminance, which keeps hue stable and bands the brightness
 * the way a comic colourist would. */
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

static void posterize_buffer(uint32_t *pixels, int w, int h) {
    int n = w * h;
    for (int i = 0; i < n; i++) pixels[i] = posterize_pixel(pixels[i]);
}

static void quantize_buffer(uint32_t *pixels, int w, int h,
                            const palette *p, int dither) {
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

/* Try to find the Valkyrie OBJ + MTL relative to common run locations
 * (repo root, the binary's own directory). Returns 1 on success, 0 if
 * none of the candidate paths exist. The OBJ's usemtl groups split into
 * one scene_mesh each; uniform scale and offset are baked into vertex
 * positions in place. */
static const char *VALKYRIE_OBJ_CANDIDATES[] = {
    "apps/mech/assets/valkyrie.obj",
    "../mech/assets/valkyrie.obj",
    "./valkyrie.obj",
};
static const char *VALKYRIE_MTL_CANDIDATES[] = {
    "apps/mech/assets/valkyrie.mtl",
    "../mech/assets/valkyrie.mtl",
    "./valkyrie.mtl",
};

static int try_load_valkyrie(scene *s, int default_mat,
                             vector offset, float uniform_scale) {
    int n_paths = (int)(sizeof(VALKYRIE_OBJ_CANDIDATES) /
                        sizeof(VALKYRIE_OBJ_CANDIDATES[0]));
    for (int i = 0; i < n_paths; i++) {
        FILE *probe = fopen(VALKYRIE_OBJ_CANDIDATES[i], "rb");
        if (!probe) continue;
        fclose(probe);

        scene_mtl_entry *mtl = NULL;
        int mtl_n = scene_load_mtl(VALKYRIE_MTL_CANDIDATES[i], &mtl);
        if (mtl_n < 0) { mtl_n = 0; mtl = NULL; }

        int first = 0;
        int added = scene_add_meshes_from_obj(s, VALKYRIE_OBJ_CANDIDATES[i],
                                              mtl, mtl_n,
                                              default_mat, &first);
        free(mtl);
        if (added <= 0) {
            fprintf(stderr, "warning: valkyrie load failed at %s\n",
                    VALKYRIE_OBJ_CANDIDATES[i]);
            return 0;
        }

        for (int k = 0; k < added; k++) {
            scene_mesh *m = &s->meshes[first + k];
            for (int v = 0; v < m->vertex_count; v++) {
                m->vertices[v].position.x = m->vertices[v].position.x * uniform_scale + offset.x;
                m->vertices[v].position.y = m->vertices[v].position.y * uniform_scale + offset.y;
                m->vertices[v].position.z = m->vertices[v].position.z * uniform_scale + offset.z;
            }
        }

        fprintf(stderr, "Loaded Valkyrie from %s (%d mesh groups)\n",
                VALKYRIE_OBJ_CANDIDATES[i], added);
        return 1;
    }
    fprintf(stderr,
            "warning: valkyrie.obj not found in any candidate path; "
            "scene will render without it\n");
    return 0;
}

static void build_scene(scene **scn, scene_camera **cam) {
    *scn = scene_create();

    /* A handful of materials chosen to read clearly at low res. The
     * pixel-art look benefits from saturated, chunky blocks of colour;
     * heavy procedural detail just turns to noise once you blit. */
    int m_red = scene_add_material(*scn, (scene_material){
        .albedo  = {220,  60,  60},
        .albedo2 = {120,  20,  20},
        .tex_kind = SCENE_TEX_CHECKER,
        .tex_scale = 0.6f,
    });
    int m_green = scene_add_material(*scn, (scene_material){
        .albedo  = {120, 200,  80},
    });
    int m_blue = scene_add_material(*scn, (scene_material){
        .albedo  = { 60, 110, 200},
    });
    int m_yellow = scene_add_material(*scn, (scene_material){
        .albedo  = {230, 200,  60},
    });
    int m_floor = scene_add_material(*scn, (scene_material){
        .albedo  = {180, 180, 180},
        .albedo2 = { 60,  60,  60},
        .tex_kind = SCENE_TEX_CHECKER,
        .tex_scale = 1.0f,
    });
    int m_mirror = scene_add_material(*scn, (scene_material){
        .reflectivity = 0.7f,
        .albedo  = {200, 200, 220},
    });

    scene_add_sphere(*scn, (scene_sphere){
        .center = {0.0f, 1.0f, 0.0f}, .radius = 1.0f, .material = m_red });
    scene_add_sphere(*scn, (scene_sphere){
        .center = {-2.4f, 0.6f, -0.5f}, .radius = 0.6f, .material = m_green });
    scene_add_sphere(*scn, (scene_sphere){
        .center = {2.0f, 0.8f, -1.0f}, .radius = 0.8f, .material = m_blue });
    scene_add_sphere(*scn, (scene_sphere){
        .center = {0.0f, 2.4f, -1.5f}, .radius = 0.9f, .material = m_mirror });

    scene_add_box(*scn, (scene_box){
        .min = {-3.5f, -0.5f, 1.5f}, .max = {-2.5f, 0.8f, 2.5f},
        .material = m_yellow });

    scene_add_cylinder(*scn, (scene_cylinder){
        .center = {2.5f, 0.5f, 2.0f}, .axis = {0.0f, 1.0f, 0.0f},
        .radius = 0.45f, .half_height = 1.0f, .material = m_yellow });

    scene_add_plane(*scn, (scene_plane){
        .point = {0.0f, -0.5f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f},
        .material = m_floor });

    /* Fallback paint material for any usemtl group in the OBJ that the
     * MTL doesn't cover. Slightly reflective so the panels catch the
     * mirror sphere's backdrop. */
    int m_paint = scene_add_material(*scn, (scene_material){
        .albedo = {220, 220, 230},
        .reflectivity = 0.08f,
    });

    /* Plant the Valkyrie behind the spheres so it both reads from the
     * default camera angle and shows up in the mirror sphere's
     * reflection. Feet on the floor (y=-0.5). */
    try_load_valkyrie(*scn, m_paint,
                      (vector){0.0f, -0.5f, -2.0f}, 1.6f);

    scene_set_ambient(*scn, 0.2f);
    scene_add_light(*scn, (scene_light){
        .direction = {1.0f, 1.2f, -0.8f}, .intensity = 0.9f });

    /* Build per-mesh BVHs for the CPU ray-mesh test; GPU backend uses
     * the same data via SSBOs. Must run after all meshes are added and
     * transforms baked. */
    rt_scene_build_accel(*scn);

    *cam = scene_camera_create(
        (vector){4.5f, 2.5f, 5.0f},
        (vector){-1.0f, -0.4f, -1.0f});
}

static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw),
    };
}

/* Mirror of rtdemo's display_pixels: nearest-neighbour blit, Y-flipped
 * so y=0 of the trace appears at the top of the window. */
static void display_pixels(GLuint tex, GLuint fbo, const uint32_t *pixels,
                           int render_w, int render_h,
                           int window_w, int window_h) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, render_w, render_h,
                    GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, render_w, render_h,
                      0, window_h, window_w, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int window_w = INIT_WINDOW_W;
    int window_h = INIT_WINDOW_H;
    int fullscreen = 0;
    SDL_Window *window = SDL_CreateWindow("Pixel-Art Raytrace",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h, SDL_WINDOW_OPENGL);
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "GL context creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(0);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    fprintf(stderr, "GL version: %s\n", (const char *)glGetString(GL_VERSION));

    rt_renderer *cpu_rnd = rt_renderer_available(RT_BACKEND_CPU)
                         ? rt_renderer_create(RT_BACKEND_CPU) : NULL;
    rt_renderer *gpu_rnd = rt_renderer_available(RT_BACKEND_OPENGL)
                         ? rt_renderer_create(RT_BACKEND_OPENGL) : NULL;
    if (!cpu_rnd && !gpu_rnd) {
        fprintf(stderr, "No renderers available\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    rt_renderer *active = gpu_rnd ? gpu_rnd : cpu_rnd;
    fprintf(stderr, "Active: %s (TAB to toggle)\n", rt_renderer_name(active));

    scene *scn;
    scene_camera *cam;
    build_scene(&scn, &cam);

    int preset = PRESET_DEFAULT;
    int render_w = PRESETS[preset].w;
    int render_h = PRESETS[preset].h;
    int palette_on = 1;
    int palette_idx = PALETTE_DEFAULT;
    int dither_on = 1;
    int posterize_on = 0;
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
    rt_viewport viewport = { render_w, render_h, FOV };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    vector cam_pos = {4.5f, 2.5f, 5.0f};
    float cam_yaw = -2.356f;
    float cam_pitch = -0.3f;
    float move_speed = 5.0f;
    float look_speed = 2.0f;
    int running = 1;

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    int fps_frames = 0;
    Uint32 render_ms_accum = 0;
    char title_buf[200];

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        frame_last = frame_now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = 0;
                if (k == SDLK_TAB) {
                    if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                    else if (active == gpu_rnd && cpu_rnd) active = cpu_rnd;
                    fprintf(stderr, "Active: %s\n", rt_renderer_name(active));
                }
                if (k == SDLK_p) {
                    palette_on = !palette_on;
                    fprintf(stderr, "Palette: %s\n", palette_on ? "on" : "off");
                }
                if (k == SDLK_LEFTBRACKET) {
                    palette_idx = (palette_idx + PALETTE_COUNT - 1) % PALETTE_COUNT;
                    fprintf(stderr, "Palette: %s\n", PALETTES[palette_idx].name);
                }
                if (k == SDLK_RIGHTBRACKET) {
                    palette_idx = (palette_idx + 1) % PALETTE_COUNT;
                    fprintf(stderr, "Palette: %s\n", PALETTES[palette_idx].name);
                }
                if (k == SDLK_h) {
                    dither_on = !dither_on;
                    fprintf(stderr, "Dither: %s\n", dither_on ? "on" : "off");
                }
                if (k == SDLK_o) {
                    posterize_on = !posterize_on;
                    fprintf(stderr, "Posterize: %s\n", posterize_on ? "on" : "off");
                }
                if (k >= SDLK_1 && k <= SDLK_4) {
                    int idx = k - SDLK_1;
                    if (idx < PRESET_COUNT) {
                        preset = idx;
                        goto recreate_buffers;
                    }
                }
                if (k == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_GetWindowSize(window, &window_w, &window_h);
                }
                continue;
                recreate_buffers:
                    render_w = PRESETS[preset].w;
                    render_h = PRESETS[preset].h;
                    free(pixels);
                    pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
                    viewport = (rt_viewport){ render_w, render_h, FOV };
                    glBindTexture(GL_TEXTURE_2D, display_tex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
                    fprintf(stderr, "Preset: %s (%dx%d)\n",
                            PRESETS[preset].name, render_w, render_h);
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_LEFT])  cam_yaw   -= look_speed * dt;
        if (keys[SDL_SCANCODE_RIGHT]) cam_yaw   += look_speed * dt;
        if (keys[SDL_SCANCODE_UP])    cam_pitch += look_speed * dt;
        if (keys[SDL_SCANCODE_DOWN])  cam_pitch -= look_speed * dt;
        if (cam_pitch >  1.4f) cam_pitch =  1.4f;
        if (cam_pitch < -1.4f) cam_pitch = -1.4f;

        vector forward = { sinf(cam_yaw), 0.0f, cosf(cam_yaw) };
        vector right   = { cosf(cam_yaw), 0.0f, -sinf(cam_yaw) };
        if (keys[SDL_SCANCODE_W]) cam_pos = vector_add(cam_pos, vector_scale(forward,  move_speed * dt));
        if (keys[SDL_SCANCODE_S]) cam_pos = vector_add(cam_pos, vector_scale(forward, -move_speed * dt));
        if (keys[SDL_SCANCODE_D]) cam_pos = vector_add(cam_pos, vector_scale(right,    move_speed * dt));
        if (keys[SDL_SCANCODE_A]) cam_pos = vector_add(cam_pos, vector_scale(right,   -move_speed * dt));
        if (keys[SDL_SCANCODE_SPACE])  cam_pos.y += move_speed * dt;
        if (keys[SDL_SCANCODE_LSHIFT]) cam_pos.y -= move_speed * dt;

        scene_camera_place(cam, cam_pos, cam_dir_from_yaw_pitch(cam_yaw, cam_pitch));

        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, scn, cam, &viewport, pixels);
        if (posterize_on) posterize_buffer(pixels, render_w, render_h);
        if (palette_on) quantize_buffer(pixels, render_w, render_h,
                                        &PALETTES[palette_idx], dither_on);
        render_ms_accum += SDL_GetTicks() - r_start;

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float avg_ms = (fps_frames > 0)
                ? (float)render_ms_accum / (float)fps_frames : 0.0f;
            const char *pal_label = palette_on ? PALETTES[palette_idx].name : "off";
            snprintf(title_buf, sizeof(title_buf),
                     "Pixel-Art Raytrace - %s %s pal=%s%s%s %d FPS (%.2f ms)",
                     rt_renderer_name(active), PRESETS[preset].name, pal_label,
                     (palette_on && dither_on) ? "+dither" : "",
                     posterize_on ? "+post" : "",
                     fps_frames, avg_ms);
            SDL_SetWindowTitle(window, title_buf);
            fprintf(stderr, "[%s %s pal=%s d=%d post=%d] %d FPS, %.2f ms\n",
                    rt_renderer_name(active), PRESETS[preset].name, pal_label,
                    dither_on, posterize_on, fps_frames, avg_ms);
            fps_frames = 0;
            render_ms_accum = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    free(pixels);
    scene_camera_destroy(cam);
    scene_destroy(scn);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
