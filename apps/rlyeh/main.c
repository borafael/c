/* R'lyeh — Lovecraftian POV demo.
 *
 * First-person stroll across a drowned alien plain. Bruised teal-purple sky,
 * two pale teal moons. Far-off jagged obsidian mountains close the world in
 * on every side. Coral-stalk vegetation scattered across the plain.
 *
 * Player can only walk and look — no jumping, no flying, no shooting.
 * The whole point is that nothing happens. The world is finished without you.
 *
 *   WASD / arrows    walk / turn
 *   mouse            look (relative-mouse mode)
 *   M                toggle mouse capture
 *   P                toggle postfx
 *   F11              fullscreen
 *   ESC              quit
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include "plane.h"
#include "cone.h"
#include "cylinder.h"
#include "heightfield.h"
#include "mesh.h"        /* rt_scene_build_accel */
#include "postfx.h"
#include "audio.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>      /* clock_gettime — benchmark timing */
#include <unistd.h>    /* sysconf — host core count for the benchmark */

#ifdef _WIN32
/* MinGW lacks POSIX setenv; _putenv_s has the same effect for our uses. */
#define setenv(k, v, ow) _putenv_s((k), (v))
#endif

#define INIT_WINDOW_W   960
#define INIT_WINDOW_H   600
#define RENDER_W        480
#define RENDER_H        300
#define FOV             (M_PI / 2.6f)
#define EYE_HEIGHT      1.7f
#define WALK_SPEED      4.0f
#define LOOK_SPEED      1.6f          /* keyboard fallback */
#define MOUSE_SENS      0.0025f
#define PITCH_LIMIT     1.45f         /* ~83°, prevents gimbal flip */
#define WAKE_FADE_SEC   4.5f          /* black -> full brightness on startup */

/* Sky gradient endpoints — pulsed each frame so the whole atmosphere
 * breathes slowly. Multiplier swings in roughly [0.84, 1.00] over a
 * ~21 s period; the gradient endpoints scale together so the
 * horizon/zenith hue relationship stays put. */
#define SKY_HORIZON_R   8
#define SKY_HORIZON_G  32
#define SKY_HORIZON_B  48
#define SKY_ZENITH_R   58
#define SKY_ZENITH_G   24
#define SKY_ZENITH_B   64
#define SKY_PULSE_RATE  0.30f          /* radians/sec */
#define SKY_PULSE_BIAS  0.92f
#define SKY_PULSE_AMP   0.08f

/* ===== Mountain heightfield (ring around player) ========================== */
#define HF_ROWS         128
#define HF_COLS         128
#define HF_WORLD_W      1700.0f
#define HF_WORLD_D      1700.0f
#define HF_INNER        260.0f       /* flat playable area radius */
#define HF_OUTER        750.0f       /* mountains reach max height here */
#define HF_MAX_H        160.0f

/* Borrowed by the scene; storage lives here. */
static float   HF_HEIGHTS[HF_ROWS * HF_COLS];
static float   HF_NORMALS[HF_ROWS * HF_COLS * 3];
static uint8_t HF_COLORS [(HF_ROWS - 1) * (HF_COLS - 1) * 3];

/* ===== Pseudo-noise (small, deterministic, no library) ==================== */
static float hash01(int x, int y) {
    uint32_t h = (uint32_t)(x * 374761393u) ^ (uint32_t)(y * 668265263u);
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) / (float)0xFFFFFFu;
}

static float smooth_noise(float x, float y) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);
    float a = hash01(ix,     iy);
    float b = hash01(ix + 1, iy);
    float c = hash01(ix,     iy + 1);
    float d = hash01(ix + 1, iy + 1);
    return a * (1 - u) * (1 - v) + b * u * (1 - v)
         + c * (1 - u) * v       + d * u * v;
}

/* ===== Whispers ========================================================== *
 *  Faint text fades in over the framebuffer at a long interval. Bottom-     *
 *  centred. ~80% intelligible English phrases, ~20% random glyph noise so   *
 *  the brain still tries to parse alien strings. Tiny embedded 5×7 lower-   *
 *  case font keeps the whole module under 250 bytes of data.                *
 * ========================================================================= */

#define FONT_W              5
#define FONT_H              7
#define FONT_GLYPH_COUNT    28
#define WHISPER_MAX_LEN     32
#define WHISPER_SCALE        2
#define WHISPER_FADE_IN_SEC  1.5f
#define WHISPER_HOLD_SEC     2.5f
#define WHISPER_FADE_OUT_SEC 2.0f
#define WHISPER_GAP_MIN_SEC 35.0f
#define WHISPER_GAP_MAX_SEC 55.0f
#define WHISPER_NOISE_PROB  20      /* percent: random-glyph instead of phrase */

/* Bit-packed 5×7 glyphs. Bit 4 (0x10) is the leftmost column. Index 0 is
 * space, index 1 is apostrophe, indices 2..27 are 'a'..'z'. */
static const uint8_t FONT_GLYPHS[FONT_GLYPH_COUNT][FONT_H] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space     */
    {0x04,0x04,0x04,0x00,0x00,0x00,0x00}, /* '         */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, /* a */
    {0x10,0x10,0x16,0x19,0x11,0x11,0x1E}, /* b */
    {0x00,0x00,0x0E,0x10,0x10,0x10,0x0E}, /* c */
    {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F}, /* d */
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, /* e */
    {0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, /* f */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, /* g */
    {0x10,0x10,0x16,0x19,0x11,0x11,0x11}, /* h */
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, /* i */
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, /* j */
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* k */
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, /* l */
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, /* m */
    {0x00,0x00,0x16,0x19,0x11,0x11,0x11}, /* n */
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, /* o */
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, /* p */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, /* q */
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, /* r */
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, /* s */
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, /* t */
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, /* u */
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, /* v */
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, /* w */
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, /* x */
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, /* y */
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, /* z */
};

static int font_index(char c) {
    if (c == ' ')  return 0;
    if (c == '\'') return 1;
    if (c >= 'a' && c <= 'z') return 2 + (c - 'a');
    return 0;
}

static const char *WHISPER_PHRASES[] = {
    "ph'nglui mglw'nafh",
    "they only sleep",
    "the angles wake",
    "the stars are wrong",
    "it remembers you",
    "yog sothoth",
    "we were here first",
    "the dream is real",
    "do not name it",
    "ia ia cthulhu",
};
#define WHISPER_PHRASE_COUNT \
    ((int)(sizeof(WHISPER_PHRASES) / sizeof(WHISPER_PHRASES[0])))

typedef enum {
    WHISPER_IDLE,
    WHISPER_FADE_IN,
    WHISPER_HOLD,
    WHISPER_FADE_OUT,
} whisper_phase;

typedef struct {
    whisper_phase phase;
    float    phase_t;     /* elapsed in current phase */
    float    next_gap;    /* idle duration before next whisper fires */
    char     text[WHISPER_MAX_LEN + 1];
    int      text_len;
    int      is_noise;    /* 1 = random glyph stream, 0 = English phrase */
    uint32_t noise_seed;  /* stable across the whole whisper lifetime */
} whisper_state;

/* Self-contained 32-bit mixer; doesn't piggy-back on hash01's two-arg form
 * because we want three orthogonal axes (seed, char index, row). */
static uint32_t whisper_hash(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 374761393u ^ b * 668265263u ^ c * 1274126177u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h;
}

static void whisper_pick_next(whisper_state *w, uint32_t seed) {
    uint32_t r = whisper_hash(seed, 0, 0xA5A5A5A5u);
    if ((r % 100u) < (uint32_t)(100 - WHISPER_NOISE_PROB)) {
        const char *src = WHISPER_PHRASES[(r >> 8) % WHISPER_PHRASE_COUNT];
        int len = 0;
        while (src[len] && len < WHISPER_MAX_LEN) {
            w->text[len] = src[len];
            len++;
        }
        w->text[len] = 0;
        w->text_len = len;
        w->is_noise = 0;
    } else {
        int len = 8 + (int)((r >> 16) % 7u);     /* 8..14 chars */
        for (int i = 0; i < len; i++) {
            w->text[i] = (char)('a' + (whisper_hash(seed, i, 1) % 26u));
        }
        w->text[len] = 0;
        w->text_len = len;
        w->is_noise = 1;
    }
    uint32_t rg = whisper_hash(seed, 1, 0xDEADBEEFu);
    w->next_gap = WHISPER_GAP_MIN_SEC
                + (WHISPER_GAP_MAX_SEC - WHISPER_GAP_MIN_SEC)
                  * ((rg & 0xFFFFu) / 65535.0f);
    w->noise_seed = seed;
}

static void whisper_update(whisper_state *w, float dt, uint32_t now_ms) {
    w->phase_t += dt;
    switch (w->phase) {
    case WHISPER_IDLE:
        if (w->phase_t >= w->next_gap) {
            whisper_pick_next(w, now_ms);
            w->phase = WHISPER_FADE_IN;
            w->phase_t = 0.0f;
        }
        break;
    case WHISPER_FADE_IN:
        if (w->phase_t >= WHISPER_FADE_IN_SEC) {
            w->phase = WHISPER_HOLD; w->phase_t = 0.0f;
        }
        break;
    case WHISPER_HOLD:
        if (w->phase_t >= WHISPER_HOLD_SEC) {
            w->phase = WHISPER_FADE_OUT; w->phase_t = 0.0f;
        }
        break;
    case WHISPER_FADE_OUT:
        if (w->phase_t >= WHISPER_FADE_OUT_SEC) {
            w->phase = WHISPER_IDLE; w->phase_t = 0.0f;
        }
        break;
    }
}

static void whisper_render(const whisper_state *w, uint32_t *pixels,
                           int W, int H) {
    if (w->phase == WHISPER_IDLE) return;
    float alpha;
    if (w->phase == WHISPER_FADE_IN) {
        float t = w->phase_t / WHISPER_FADE_IN_SEC;
        alpha = t * t * (3.0f - 2.0f * t);
    } else if (w->phase == WHISPER_HOLD) {
        alpha = 1.0f;
    } else {                                     /* FADE_OUT */
        float t = 1.0f - w->phase_t / WHISPER_FADE_OUT_SEC;
        if (t < 0.0f) t = 0.0f;
        alpha = t * t * (3.0f - 2.0f * t);
    }
    int alpha_i = (int)(alpha * 255.0f);
    if (alpha_i <= 0) return;
    if (alpha_i > 255) alpha_i = 255;

    int char_step = (FONT_W + 1) * WHISPER_SCALE;
    int text_w = w->text_len * char_step;
    int text_h = FONT_H * WHISPER_SCALE;
    int x0 = (W - text_w) / 2;
    int y0 = H - text_h - 16;

    const int fg_r = 200, fg_g = 220, fg_b = 230;   /* pale teal-white */

    for (int ci = 0; ci < w->text_len; ci++) {
        uint8_t glyph[FONT_H];
        if (w->is_noise) {
            for (int r = 0; r < FONT_H; r++) {
                glyph[r] = (uint8_t)(whisper_hash(w->noise_seed, ci, r) & 0x1F);
            }
        } else {
            int gi = font_index(w->text[ci]);
            for (int r = 0; r < FONT_H; r++) glyph[r] = FONT_GLYPHS[gi][r];
        }
        for (int gy = 0; gy < FONT_H; gy++) {
            uint8_t row = glyph[gy];
            if (!row) continue;
            for (int gx = 0; gx < FONT_W; gx++) {
                if (!(row & (1 << (FONT_W - 1 - gx)))) continue;
                int px0 = x0 + ci * char_step + gx * WHISPER_SCALE;
                int py0 = y0 + gy * WHISPER_SCALE;
                for (int sy = 0; sy < WHISPER_SCALE; sy++) {
                    int py = py0 + sy;
                    if (py < 0 || py >= H) continue;
                    for (int sx = 0; sx < WHISPER_SCALE; sx++) {
                        int px = px0 + sx;
                        if (px < 0 || px >= W) continue;
                        uint32_t p = pixels[py * W + px];
                        int b  = ( p        & 0xFF);
                        int g  = ((p >>  8) & 0xFF);
                        int r2 = ((p >> 16) & 0xFF);
                        b  = (b  * (255 - alpha_i) + fg_b * alpha_i) >> 8;
                        g  = (g  * (255 - alpha_i) + fg_g * alpha_i) >> 8;
                        r2 = (r2 * (255 - alpha_i) + fg_r * alpha_i) >> 8;
                        pixels[py * W + px] = (p & 0xFF000000u)
                                            | ((uint32_t)r2 << 16)
                                            | ((uint32_t)g  <<  8)
                                            |  (uint32_t)b;
                    }
                }
            }
        }
    }
}

static float fbm(float x, float y) {
    float s = 0, amp = 1, freq = 1, sum = 0;
    for (int i = 0; i < 4; i++) {
        s += amp * smooth_noise(x * freq, y * freq);
        sum += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return s / sum;
}

static void build_mountains(void) {
    for (int r = 0; r < HF_ROWS; r++) {
        for (int c = 0; c < HF_COLS; c++) {
            float u = (float)c / (HF_COLS - 1) - 0.5f;
            float v = (float)r / (HF_ROWS - 1) - 0.5f;
            float wx = u * HF_WORLD_W;
            float wz = v * HF_WORLD_D;
            float d  = sqrtf(wx * wx + wz * wz);

            float ramp = 0.0f;
            if (d > HF_INNER) {
                float t = (d - HF_INNER) / (HF_OUTER - HF_INNER);
                if (t > 1.0f) t = 1.0f;
                /* Squared ramp = gentle near, jagged far. */
                ramp = t * t;
            }
            float n1 = fbm(c * 0.22f, r * 0.22f);
            float n2 = fbm(c * 0.65f, r * 0.65f);
            float n3 = fbm(c * 1.40f, r * 1.40f);
            /* Ridge term: peaks where noise crosses 0.5, valleys elsewhere.
             * Multiplied in to break long ridgelines into a denser saw of
             * peaks without raising the overall envelope. */
            float ridge = 1.0f - fabsf(2.0f * n3 - 1.0f);
            float h  = ramp * HF_MAX_H
                     * (0.30f + 1.00f * n1)
                     * (0.50f + 0.90f * n2)
                     * (0.65f + 0.50f * ridge);

            /* Subtle ripple in the inner playable area — a hair of tide
             * pools so the floor doesn't read as perfectly mathematical. */
            if (d < HF_INNER) {
                h = 0.08f * (fbm(c * 0.6f, r * 0.6f) - 0.5f);
            }
            HF_HEIGHTS[r * HF_COLS + c] = h;
        }
    }
    float dx = HF_WORLD_W / (HF_COLS - 1);
    float dz = HF_WORLD_D / (HF_ROWS - 1);
    for (int r = 0; r < HF_ROWS; r++) {
        for (int c = 0; c < HF_COLS; c++) {
            int cl = c > 0           ? c - 1 : c;
            int cr = c < HF_COLS - 1 ? c + 1 : c;
            int rt = r > 0           ? r - 1 : r;
            int rb = r < HF_ROWS - 1 ? r + 1 : r;
            float hl = HF_HEIGHTS[r  * HF_COLS + cl];
            float hr = HF_HEIGHTS[r  * HF_COLS + cr];
            float ht = HF_HEIGHTS[rt * HF_COLS + c];
            float hb = HF_HEIGHTS[rb * HF_COLS + c];
            vector n = {
                -(hr - hl) / ((cr - cl) * dx),
                 1.0f,
                -(hb - ht) / ((rb - rt) * dz)
            };
            n = vector_normalize(n);
            HF_NORMALS[(r * HF_COLS + c) * 3 + 0] = n.x;
            HF_NORMALS[(r * HF_COLS + c) * 3 + 1] = n.y;
            HF_NORMALS[(r * HF_COLS + c) * 3 + 2] = n.z;
        }
    }
    /* Per-cell colors — wet obsidian, slightly teal-pooled in the valleys,
     * fading to near-black at the peaks. */
    for (int r = 0; r < HF_ROWS - 1; r++) {
        for (int c = 0; c < HF_COLS - 1; c++) {
            float h = HF_HEIGHTS[r * HF_COLS + c];
            float t = h / HF_MAX_H;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            /* Low ground: teal-tinged dark; high peaks: bluer black. */
            float rch = 6.0f  * (1.0f - t) +  3.0f * t;
            float gch = 18.0f * (1.0f - t) +  5.0f * t;
            float bch = 28.0f * (1.0f - t) + 10.0f * t;
            int ci = (r * (HF_COLS - 1) + c) * 3;
            HF_COLORS[ci + 0] = (uint8_t)rch;
            HF_COLORS[ci + 1] = (uint8_t)gch;
            HF_COLORS[ci + 2] = (uint8_t)bch;
        }
    }
}

/* ===== Scene construction ================================================ */
static void add_star_field(scene *s, int mat, int count) {
    /* Stars on a hemisphere; positions are pseudo-random but deterministic. */
    for (int i = 0; i < count; i++) {
        float u = hash01(i, 11);
        float v = hash01(i, 47);
        float theta = u * 2.0f * (float)M_PI;
        /* Bias toward upper hemisphere — clamp v so phi stays > 30° above horizon. */
        float phi = (0.18f + 0.55f * v) * (float)M_PI;   /* polar angle from +Y */
        float r = 1350.0f + 80.0f * hash01(i, 73);
        float x = r * sinf(phi) * cosf(theta);
        float y = r * cosf(phi);
        float z = r * sinf(phi) * sinf(theta);
        float radius = 0.6f + 1.6f * hash01(i, 19);
        scene_add_sphere(s, (scene_sphere){
            .center = {x, y, z}, .radius = radius, .material = mat,
        });
    }
}

/* Bases for per-frame animated spheres. Filled in at scene build time
 * and read back each frame; the in-scene sphere centers are then
 * rewritten as base + animated offset so motion never compounds. */
static vector CTHULHU_BASE;
static int    CTHULHU_IDX = -1;

/* Leaning coral stalks. The cone primitive stores apex + axis; to lean
 * the tip toward the camera while keeping the root planted, we keep
 * each stalk's BASE (rooted) position and its rest-tilt magnitude, and
 * recompute apex + axis per frame from the smoothed lean direction.
 * The smoothing time-constant is large (~25 s) so the motion reads as
 * "watching" rather than "tracking". */
#define VEG_COUNT 14
static int    VEG_FIRST_IDX = -1;
static vector VEG_BASE  [VEG_COUNT];   /* root position (y ≈ 0) */
static float  VEG_HEIGHT[VEG_COUNT];
static float  VEG_TILT  [VEG_COUNT];   /* rest tilt magnitude (length of horizontal axis component) */
static float  VEG_LEAN_X[VEG_COUNT];   /* smoothed unit horizontal direction the tip leans toward */
static float  VEG_LEAN_Z[VEG_COUNT];
#define VEG_LEAN_RATE  0.04f           /* per-second smoothing factor (~25 s response) */


static void add_vegetation(scene *s, int stalk_mat) {
    /* Coral / polyp clumps scattered across the playable area. Each clump
     * is a tall asymmetric cone tilted slightly off-vertical. Positions
     * are seeded but pseudo-random; orientation is varied. */
    static const struct { float x, z, h, r, tilt; } clumps[] = {
        { -18.0f,  12.0f, 5.2f, 0.55f, 0.05f },
        {  22.0f, -14.0f, 3.8f, 0.45f, 0.12f },
        { -32.0f, -25.0f, 6.5f, 0.65f, 0.08f },
        {  35.0f,  28.0f, 4.4f, 0.40f, 0.15f },
        {   8.0f,  44.0f, 7.2f, 0.70f, 0.04f },
        { -12.0f, -48.0f, 5.8f, 0.55f, 0.11f },
        {  48.0f,  -4.0f, 4.1f, 0.42f, 0.18f },
        {  62.0f,  38.0f, 6.0f, 0.58f, 0.07f },
        { -55.0f,  22.0f, 4.7f, 0.46f, 0.13f },
        {  28.0f, -58.0f, 6.8f, 0.62f, 0.06f },
        { -40.0f,  56.0f, 5.1f, 0.48f, 0.16f },
        {  14.0f,  70.0f, 7.6f, 0.72f, 0.05f },
        { -68.0f, -30.0f, 4.3f, 0.44f, 0.17f },
        {  52.0f,  64.0f, 5.9f, 0.54f, 0.09f },
    };
    int n = (int)(sizeof(clumps) / sizeof(clumps[0]));
    for (int i = 0; i < n; i++) {
        float th = (float)i * 0.7f;
        vector apex = { clumps[i].x, clumps[i].h, clumps[i].z };
        vector axis = vector_normalize((vector){
            clumps[i].tilt * cosf(th), -1.0f, clumps[i].tilt * sinf(th)
        });
        int idx = scene_add_cone(s, (scene_cone){
            .apex     = apex,
            .axis     = axis,
            .height   = clumps[i].h,
            .radius   = clumps[i].r,
            .material = stalk_mat,
        });
        if (i < VEG_COUNT) {
            if (VEG_FIRST_IDX < 0) VEG_FIRST_IDX = idx;
            /* Treat the root as approximately directly under the apex;
             * with tilt ≤ 0.18 the horizontal offset is well under 1
             * unit, which the slow lean update absorbs harmlessly. */
            VEG_BASE  [i] = (vector){ clumps[i].x, 0.0f, clumps[i].z };
            VEG_HEIGHT[i] = clumps[i].h;
            VEG_TILT  [i] = clumps[i].tilt;
            VEG_LEAN_X[i] = cosf(th);
            VEG_LEAN_Z[i] = sinf(th);
        }
    }
}

static void add_coral_cluster(scene *s, int stalk_mat, float cx, float cz) {
    /* A denser knot of stalks placed in one direction from spawn so the
     * eye is drawn to walk toward it. Position offsets are hand-picked
     * for asymmetry — never on a grid. Stays static (does not lean);
     * the moving stalks are the original seven near spawn. */
    static const struct { float dx, dz, h, r, tilt_az; } cluster[] = {
        {  0.0f,  0.0f, 6.8f, 0.60f, 0.10f },
        {  1.8f, -1.2f, 5.4f, 0.45f, 0.15f },
        { -2.2f,  0.8f, 7.2f, 0.55f, 0.08f },
        {  0.6f,  2.4f, 4.6f, 0.40f, 0.12f },
        { -1.0f, -2.6f, 5.8f, 0.50f, 0.14f },
        {  2.6f,  1.4f, 6.2f, 0.45f, 0.09f },
        { -2.8f, -0.4f, 4.2f, 0.38f, 0.18f },
    };
    int n = (int)(sizeof(cluster) / sizeof(cluster[0]));
    for (int i = 0; i < n; i++) {
        float th = (float)i * 1.13f;
        vector apex = { cx + cluster[i].dx, cluster[i].h, cz + cluster[i].dz };
        vector axis = vector_normalize((vector){
            cluster[i].tilt_az * cosf(th), -1.0f, cluster[i].tilt_az * sinf(th)
        });
        scene_add_cone(s, (scene_cone){
            .apex     = apex,
            .axis     = axis,
            .height   = cluster[i].h,
            .radius   = cluster[i].r,
            .material = stalk_mat,
        });
    }
}

static void build_scene(scene **out_s, scene_camera **out_cam, int *out_sky_mat) {
    scene *s = scene_create();

    /* ===== Materials ===== */
    /* Sky: dark teal at horizon, bleeding to bruised purple at zenith via
     * GRADIENT (albedo at bottom -> albedo2 at top), unlit. The sphere
     * is huge so the gradient stretches across the whole field of view. */
    int m_sky = scene_add_material(s, (scene_material){
        .albedo   = { SKY_HORIZON_R, SKY_HORIZON_G, SKY_HORIZON_B },
        .albedo2  = { SKY_ZENITH_R,  SKY_ZENITH_G,  SKY_ZENITH_B  },
        .tex_kind = SCENE_TEX_GRADIENT,
        .tex_scale = 1400.0f,        /* span = sky-sphere diameter */
        .unlit = 1,
    });
    int m_moon = scene_add_material(s, (scene_material){
        .albedo = {130, 170, 175}, .unlit = 1,
    });
    int m_moon2 = scene_add_material(s, (scene_material){
        .albedo = {95, 145, 155}, .unlit = 1,
    });
    int m_star = scene_add_material(s, (scene_material){
        .albedo = {220, 230, 235}, .unlit = 1,
    });
    /* Heightfield uses the per-cell colors baked into HF_COLORS, no
     * material needed. Pass -1 for the heightfield material to skip the
     * material-modulation path entirely. */

    /* Coral stalks — dark with cyan/teal marble veins, very low
     * reflectivity (wet but not mirror). */
    int m_stalk = scene_add_material(s, (scene_material){
        .albedo   = { 18,  35,  45},
        .albedo2  = { 70, 140, 150},
        .tex_kind = SCENE_TEX_MARBLE,
        .tex_scale = 1.2f,
        .reflectivity = 0.08f,
    });
    /* ===== Geometry ===== */
    /* Sky sphere — huge, centered on origin, gradient is along +Y.
     * Radius has to clear the star hemisphere (r≈1430) and the mountain
     * corners (HF_WORLD_W * sqrt(2)/2 ≈ 1202). */
    scene_add_sphere(s, (scene_sphere){
        .center = {0, 0, 0}, .radius = 1700.0f, .material = m_sky,
    });

    /* Two pale teal moons — high and oblique. */
    scene_add_sphere(s, (scene_sphere){
        .center = {-280.0f, 200.0f, 80.0f}, .radius = 24.0f, .material = m_moon,
    });
    scene_add_sphere(s, (scene_sphere){
        .center = {220.0f, 240.0f, -120.0f}, .radius = 18.0f, .material = m_moon2,
    });

    /* Stars — deterministic pseudo-random hemisphere. */
    add_star_field(s, m_star, 90);

    /* Mountains — heightfield ring around the player. */
    build_mountains();
    scene_heightfield hf = {
        .heights     = HF_HEIGHTS,
        .normals     = HF_NORMALS,
        .colors      = HF_COLORS,
        .rows        = HF_ROWS,
        .cols        = HF_COLS,
        .world_width = HF_WORLD_W,
        .world_depth = HF_WORLD_D,
        .origin_x    = -HF_WORLD_W * 0.5f,
        .origin_z    = -HF_WORLD_D * 0.5f,
        .max_height  = HF_MAX_H,
        .material    = -1,         /* raw cell colors, no material modulation */
    };
    scene_add_heightfield(s, &hf);

    /* Vegetation — the seven leaners near spawn. */
    add_vegetation(s, m_stalk);

    /* Coral cluster as destination — denser knot forward-right of
     * spawn (still well inside HF_INNER) so the eye has something
     * to walk toward, between the player and the +Z horizon. */
    add_coral_cluster(s, m_stalk, 18.0f, 95.0f);

    /* Cthulhu silhouette — a huge unlit sphere high in the sky, just
     * a touch darker than the zenith gradient so it reads as a hole
     * in the heavens rather than a body floating in front. Positioned
     * off-axis (forward-right, elevation ~60°) so the player has to
     * actually look up to notice it. Substantial angular footprint
     * (~17°), but its near-sky colour keeps it from screaming for
     * attention. One sphere; almost free to render. */
    int m_cthulhu = scene_add_material(s, (scene_material){
        .albedo = {28, 12, 34}, .unlit = 1,
    });
    CTHULHU_BASE = (vector){325.0f, 1126.0f, 563.0f};
    CTHULHU_IDX = scene_add_sphere(s, (scene_sphere){
        .center = CTHULHU_BASE, .radius = 200.0f, .material = m_cthulhu,
    });

    /* Distant monolith — a single obsidian spire forward-left of
     * spawn, just inside the mountain ring. Tall and disproportionately
     * thin so the silhouette reads as wrong even before you parse what
     * it is. Slight tilt toward the camera to make the geometry feel
     * a touch off-axis. Fog still applies, so it hazes with depth like
     * the rest of the world — the eeriness is in the shape, not in
     * cheating the atmosphere. */
    int m_monolith = scene_add_material(s, (scene_material){
        .albedo       = { 15,  18,  26},   /* dark cool-violet obsidian */
        .reflectivity = 0.04f,
    });
    scene_add_cylinder(s, (scene_cylinder){
        .center      = {-200.0f, 110.0f, 480.0f},
        .axis        = vector_normalize((vector){0.04f, 1.0f, -0.02f}),
        .radius      = 10.0f,
        .half_height = 110.0f,
        .material    = m_monolith,
    });

    /* ===== Lights ===== */
    /* Two directionals: one key from the +Z horizon, one faint fill
     * from the opposite zenith (cold teal-violet). Low ambient keeps
     * shadows heavy without going pitch-black. */
    scene_set_ambient(s, 0.18f);
    scene_add_light(s, (scene_light){
        .direction = vector_normalize((vector){0.0f, 0.30f, 1.0f}),
        .intensity = 0.70f,
    });
    scene_add_light(s, (scene_light){
        .direction = vector_normalize((vector){-0.4f, 0.55f, -0.6f}),
        .intensity = 0.28f,
    });

    rt_scene_build_accel(s);

    *out_s   = s;
    *out_cam = scene_camera_create(
        (vector){0.0f, EYE_HEIGHT, 0.0f},
        (vector){0.0f, 0.0f, 1.0f}
    );
    if (out_sky_mat) *out_sky_mat = m_sky;
}

/* ===== POV helpers ======================================================= */
static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw)
    };
}

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

/* ===== Headless benchmark (RLYEH_BENCH=1) ================================ *
 * Measures the cost of adding a fully-reflective roaming sphere to the
 * real R'lyeh scene, with no SDL/GL (so it runs anywhere). Three
 * conditions on the identical scene + camera:
 *   baseline        — scene as shipped
 *   +occluder(r=0)  — one extra unlit sphere in the sky (isolates the
 *                     "+1 primitive in every scan" cost; no bounce)
 *   +mirror(r=1)    — same sphere flipped to a perfect mirror (adds the
 *                     reflection-bounce cost on its covered pixels)
 * Each is timed full-frame and again with the app's even-rows interlace.
 * The sphere's real screen coverage is counted from the G-buffer so we
 * can report a per-covered-pixel marginal cost. */
static double bench_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

static double bench_measure(rt_renderer *rnd, scene *s, scene_camera *cam,
                            rt_viewport *vp, uint32_t *px, rt_gbuffer *gb,
                            int n, double *out_min, double *out_max) {
    for (int i = 0; i < 5; i++)                 /* warm caches / clocks */
        rt_renderer_render(rnd, s, cam, vp, px, gb);
    double total = 0.0, mn = 1e30, mx = 0.0;
    for (int i = 0; i < n; i++) {
        double t0 = bench_now_ms();
        rt_renderer_render(rnd, s, cam, vp, px, gb);
        double dt = bench_now_ms() - t0;
        total += dt;
        if (dt < mn) mn = dt;
        if (dt > mx) mx = dt;
    }
    if (out_min) *out_min = mn;
    if (out_max) *out_max = mx;
    return total / (double)n;
}

static int run_bench(void) {
    /* The scene ships with no reflective spheres, so the baseline below is
     * naturally clean: the occluder->mirror deltas isolate exactly the one
     * sphere this benchmark adds. */
    scene *s = NULL; scene_camera *cam = NULL; int sky = -1;
    build_scene(&s, &cam, &sky);

    /* Fixed representative pose: spawn point, level, looking +Z. The
     * roaming sphere (placed below) hangs in the upper third of this view. */
    scene_camera_place(cam, (vector){0.0f, EYE_HEIGHT, 0.0f},
                            (vector){0.0f, 0.0f, 1.0f});

    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!rnd) { fprintf(stderr, "CPU renderer unavailable\n"); return 1; }

    int W = RENDER_W, H = RENDER_H;
    rt_viewport vp = { W, H, FOV };
    uint32_t *px = calloc((size_t)(W * H), sizeof(uint32_t));
    rt_gbuffer gb = {
        .object_id = calloc((size_t)(W * H), sizeof(uint32_t)),
        .depth     = calloc((size_t)(W * H), sizeof(float)),
        .normal    = calloc((size_t)(W * H) * 3, sizeof(float)),
    };

    const char *n_env     = getenv("RLYEH_BENCH_FRAMES");
    const int   N         = n_env ? atoi(n_env) : 120;
    const int   fields[2] = { -1, 0 };                 /* full frame, app interlace */
    const char *fname[2]  = { "full-frame    ", "interlaced(app)" };

    /* Deltas are computed off MIN, not AVG: at these frame times the
     * thread pool's scheduling jitter (tens of %) dwarfs the sphere's
     * sub-ms cost, and the least-contended run is the cleanest estimate
     * of the actual compute. AVG/MAX are printed for context only. */
    long nthreads = sysconf(_SC_NPROCESSORS_ONLN);
    const char *thr_env = getenv("RT_CPU_THREADS");
    printf("R'lyeh reflective-sphere benchmark\n");
    printf("  res=%dx%d  bounce_budget=4  frames/measure=%d  host_cores=%ld  RT_CPU_THREADS=%s\n",
           W, H, N, nthreads, thr_env ? thr_env : "(default=all)");
    printf("  base scene: %d spheres, %d cones, %d cylinders, %d heightfields\n\n",
           s->sphere_count, s->cone_count, s->cylinder_count,
           s->heightfield_count);

    double base_ms[2], occ_ms[2], mir_ms[2];
    double base_mn[2], occ_mn[2], mir_mn[2], mx;

    /* --- baseline (no extra sphere) --- */
    for (int f = 0; f < 2; f++) {
        rt_renderer_set_interlace(rnd, fields[f]);
        base_ms[f] = bench_measure(rnd, s, cam, &vp, px, &gb, N, &base_mn[f], &mx);
        printf("  baseline        %s : min %6.3f ms  (avg %6.3f / max %6.3f)\n",
               fname[f], base_mn[f], base_ms[f], mx);
    }

    /* --- add the roaming sphere as an unlit occluder (reflectivity 0) --- */
    int mat = scene_add_material(s, (scene_material){
        .albedo = {40, 40, 55}, .unlit = 1,
    });
    int sidx = scene_add_sphere(s, (scene_sphere){
        .center = {0.0f, 233.0f, 500.0f}, .radius = 80.0f, .material = mat,
    });
    uint32_t want = ((uint32_t)RT_OBJ_KIND_SPHERE << 24)
                  | ((uint32_t)sidx & 0x00FFFFFFu);
    for (int f = 0; f < 2; f++) {
        rt_renderer_set_interlace(rnd, fields[f]);
        occ_ms[f] = bench_measure(rnd, s, cam, &vp, px, &gb, N, &occ_mn[f], &mx);
        printf("  +occluder(r=0)  %s : min %6.3f ms  (avg %6.3f / max %6.3f)\n",
               fname[f], occ_mn[f], occ_ms[f], mx);
    }
    /* Coverage from the last (interlaced) occluder pass would only count
     * even rows; re-render once full-frame so the count is the true
     * on-screen footprint. */
    rt_renderer_set_interlace(rnd, -1);
    rt_renderer_render(rnd, s, cam, &vp, px, &gb);
    int cover = 0;
    for (int i = 0; i < W * H; i++) if (gb.object_id[i] == want) cover++;

    /* --- flip the same sphere to a perfect mirror --- */
    s->materials[mat].unlit        = 0;
    s->materials[mat].reflectivity = 1.0f;
    for (int f = 0; f < 2; f++) {
        rt_renderer_set_interlace(rnd, fields[f]);
        mir_ms[f] = bench_measure(rnd, s, cam, &vp, px, &gb, N, &mir_mn[f], &mx);
        printf("  +mirror(r=1)    %s : min %6.3f ms  (avg %6.3f / max %6.3f)\n",
               fname[f], mir_mn[f], mir_ms[f], mx);
    }

    double cov_pct = 100.0 * (double)cover / (double)(W * H);
    printf("\n  sphere screen coverage: %d / %d px (%.2f%% of frame)\n",
           cover, W * H, cov_pct);
    printf("  --- deltas vs baseline, from MIN (full-frame) ---\n");
    printf("    occluder (+1 sphere in scan): %+6.3f ms (%+.1f%%)\n",
           occ_mn[0] - base_mn[0],
           100.0 * (occ_mn[0] - base_mn[0]) / base_mn[0]);
    printf("    mirror   (occluder + bounce): %+6.3f ms (%+.1f%%)\n",
           mir_mn[0] - base_mn[0],
           100.0 * (mir_mn[0] - base_mn[0]) / base_mn[0]);
    if (cover > 0) {
        double per_px_us = 1000.0 * (mir_mn[0] - occ_mn[0]) / (double)cover;
        printf("    reflection cost: %+6.3f ms over %d px = %.3f us/covered px\n",
               mir_mn[0] - occ_mn[0], cover, per_px_us);
        printf("    extrapolated if it filled the whole frame: %.2f ms "
               "(+%.0f%% over baseline)\n",
               per_px_us * (double)(W * H) / 1000.0,
               100.0 * (per_px_us * (double)(W * H) / 1000.0) / base_mn[0]);
    }

    free(gb.normal); free(gb.depth); free(gb.object_id); free(px);
    scene_camera_destroy(cam);
    scene_destroy(s);
    rt_renderer_destroy(rnd);
    return 0;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (getenv("RLYEH_BENCH")) return run_bench();

    /* CPU raytrace; interlace on for ~2x frame rate. The dropped rows
     * just hold the previous frame and read as crawling persistence on
     * motion, which fits the mood. */
    setenv("RT_CPU_INTERLACE", "0", 1);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Ambient loop — failure is non-fatal, the demo just runs silent.
     * Two probe paths so the same binary works from the project root
     * (dev) and from a staged dir next to its `assets/` (Win64 build). */
    if (audio_init("apps/rlyeh/assets/ambient.mp3") != 0)
        audio_init("assets/ambient.mp3");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int window_w = INIT_WINDOW_W, window_h = INIT_WINDOW_H;
    int fullscreen = 0;
    SDL_Window *window = SDL_CreateWindow("R'lyeh",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h, SDL_WINDOW_OPENGL);
    if (!window) {
        fprintf(stderr, "Window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "GL ctx: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(0);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!rnd) {
        fprintf(stderr, "CPU renderer unavailable\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    scene *scn = NULL;
    scene_camera *cam = NULL;
    int sky_mat = -1;
    build_scene(&scn, &cam, &sky_mat);

    int render_w = RENDER_W, render_h = RENDER_H;
    rt_viewport viewport = { render_w, render_h, FOV };
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));

    /* G-buffer drives the distance-fog pass; depth and object_id are
     * needed, normal is unused but the renderer fills all three. */
    rt_gbuffer gbuf = {
        .object_id = calloc((size_t)(render_w * render_h), sizeof(uint32_t)),
        .depth     = calloc((size_t)(render_w * render_h), sizeof(float)),
        .normal    = calloc((size_t)(render_w * render_h) * 3, sizeof(float)),
    };

    /* Postfx stack — fog first (so bloom blooms the foggy frame and
     * bright glints spill over hazed mountains), then chromatic +
     * vignette + grain on top. */
    postfx_chromatic_ctx *chrom = postfx_chromatic_create(render_w, render_h);
    postfx_bloom_ctx     *bloom = postfx_bloom_create(render_w, render_h);
    postfx_chromatic chrom_cfg = { .enabled = 1, .shift_pixels = 1 };
    postfx_vignette  vig_cfg   = { .enabled = 1, .intensity = 0.35f, .softness = 0.50f };
    postfx_grain     grain_cfg = { .enabled = 1, .strength = 0.10f, .seed = 0 };
    postfx_bloom     bloom_cfg = {
        .enabled = 1, .threshold = 0.55f, .knee = 0.30f,
        .intensity = 0.55f, .radius = 6, .iterations = 2,
    };
    /* Fog targets the horizon teal so distant mountains fade into the
     * sky gradient. Skip sky/moons/stars (all spheres) — they
     * already paint their own backdrop colours. The ramp starts past
     * the coral-stalk radius and saturates a bit beyond the mountain
     * ring so far peaks crush hard into the haze. */
    postfx_fog fog_cfg = {
        .enabled       = 1,
        .color         = { 14, 30, 44 },
        .start         = 120.0f,
        .end           = 700.0f,
        .max_strength  = 1.00f,
        .skip_kinds_mask = (1u << RT_OBJ_KIND_SKY)
                         | (1u << RT_OBJ_KIND_SPHERE),
    };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    /* Player state — at origin, looking down +Z across the plain. */
    vector cam_pos   = {0.0f, EYE_HEIGHT, 0.0f};
    float  cam_yaw   = 0.0f;     /* 0 = +Z */
    float  cam_pitch = 0.0f;
    int    mouse_captured = 1;
    int    postfx_on = 1;

    /* Whisper state — starts idle, first fire is between WHISPER_GAP_MIN
     * and WHISPER_GAP_MAX seconds after the wake fade finishes. */
    whisper_state whisper = {0};
    whisper.phase = WHISPER_IDLE;
    whisper.next_gap = WAKE_FADE_SEC + WHISPER_GAP_MIN_SEC;

    SDL_SetRelativeMouseMode(SDL_TRUE);

    int running = 1;
    Uint32 start_ticks = SDL_GetTicks();
    Uint32 frame_last  = start_ticks;
    Uint32 fps_last    = start_ticks;
    int    fps_frames = 0;
    Uint32 r_ms = 0, fx_ms = 0;
    char title_buf[200];

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        frame_last = frame_now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = 0;
                if (k == SDLK_m) {
                    mouse_captured = !mouse_captured;
                    SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
                }
                if (k == SDLK_p) postfx_on = !postfx_on;
                if (k == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_GetWindowSize(window, &window_w, &window_h);
                }
            }
            if (e.type == SDL_MOUSEMOTION && mouse_captured) {
                cam_yaw   += e.motion.xrel * MOUSE_SENS;
                cam_pitch -= e.motion.yrel * MOUSE_SENS;
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        /* Arrow keys = look (fallback when mouse capture is off). */
        if (keys[SDL_SCANCODE_LEFT])  cam_yaw   -= LOOK_SPEED * dt;
        if (keys[SDL_SCANCODE_RIGHT]) cam_yaw   += LOOK_SPEED * dt;
        if (keys[SDL_SCANCODE_UP])    cam_pitch += LOOK_SPEED * dt;
        if (keys[SDL_SCANCODE_DOWN])  cam_pitch -= LOOK_SPEED * dt;
        if (cam_pitch >  PITCH_LIMIT) cam_pitch =  PITCH_LIMIT;
        if (cam_pitch < -PITCH_LIMIT) cam_pitch = -PITCH_LIMIT;

        /* WASD = walk on the plain. Movement is yaw-aligned, no pitch
         * (so looking up doesn't levitate you). Y is locked to eye height. */
        vector fwd   = { sinf(cam_yaw), 0.0f, cosf(cam_yaw) };
        vector right = { cosf(cam_yaw), 0.0f, -sinf(cam_yaw) };
        float v = WALK_SPEED * dt;
        if (keys[SDL_SCANCODE_W]) cam_pos = vector_add(cam_pos, vector_scale(fwd,    v));
        if (keys[SDL_SCANCODE_S]) cam_pos = vector_add(cam_pos, vector_scale(fwd,   -v));
        if (keys[SDL_SCANCODE_D]) cam_pos = vector_add(cam_pos, vector_scale(right,  v));
        if (keys[SDL_SCANCODE_A]) cam_pos = vector_add(cam_pos, vector_scale(right, -v));
        cam_pos.y = EYE_HEIGHT;

        vector cam_dir = cam_dir_from_yaw_pitch(cam_yaw, cam_pitch);
        scene_camera_place(cam, cam_pos, cam_dir);

        float t_sec = (frame_now - start_ticks) / 1000.0f;

        /* Slow sky pulse — the atmosphere breathes. Cheap: two color
         * triples scaled by one multiplier per frame, no per-pixel
         * cost. */
        if (sky_mat >= 0) {
            float pulse = SKY_PULSE_BIAS
                        + SKY_PULSE_AMP * sinf(t_sec * SKY_PULSE_RATE);
            scn->materials[sky_mat].albedo.r  = (uint8_t)(SKY_HORIZON_R * pulse);
            scn->materials[sky_mat].albedo.g  = (uint8_t)(SKY_HORIZON_G * pulse);
            scn->materials[sky_mat].albedo.b  = (uint8_t)(SKY_HORIZON_B * pulse);
            scn->materials[sky_mat].albedo2.r = (uint8_t)(SKY_ZENITH_R  * pulse);
            scn->materials[sky_mat].albedo2.g = (uint8_t)(SKY_ZENITH_G  * pulse);
            scn->materials[sky_mat].albedo2.b = (uint8_t)(SKY_ZENITH_B  * pulse);
        }

        /* Cthulhu drift — slow lissajous in xz, gentler bob in y.
         * Three slightly off-rate sinusoids so the orbit never closes
         * cleanly; the eye reads it as "moving" without being able to
         * pin down a path. */
        if (CTHULHU_IDX >= 0 && CTHULHU_IDX < scn->sphere_count) {
            scn->spheres[CTHULHU_IDX].center.x = CTHULHU_BASE.x + 38.0f * sinf(t_sec * 0.18f);
            scn->spheres[CTHULHU_IDX].center.z = CTHULHU_BASE.z + 28.0f * cosf(t_sec * 0.13f);
            scn->spheres[CTHULHU_IDX].center.y = CTHULHU_BASE.y + 12.0f * sinf(t_sec * 0.11f);
        }

        /* Coral stalks lean toward the camera with a long lag. Each
         * frame: compute target unit direction from base to camera (xz
         * only), smooth toward it at VEG_LEAN_RATE, then synthesise a
         * fresh apex + axis pair so the cone leans with that direction
         * while the root stays planted. The lerp is dt-scaled so the
         * response is frame-rate-independent. */
        if (VEG_FIRST_IDX >= 0) {
            for (int i = 0; i < VEG_COUNT; i++) {
                int ci = VEG_FIRST_IDX + i;
                if (ci >= scn->cone_count) break;
                float dx = cam_pos.x - VEG_BASE[i].x;
                float dz = cam_pos.z - VEG_BASE[i].z;
                float horiz = sqrtf(dx * dx + dz * dz);
                if (horiz > 0.001f) { dx /= horiz; dz /= horiz; }
                else { dx = VEG_LEAN_X[i]; dz = VEG_LEAN_Z[i]; }
                float k = dt * VEG_LEAN_RATE * 25.0f;   /* normalize to ~1 at the time constant */
                if (k > 1.0f) k = 1.0f;
                VEG_LEAN_X[i] += (dx - VEG_LEAN_X[i]) * k;
                VEG_LEAN_Z[i] += (dz - VEG_LEAN_Z[i]) * k;
                float lx = VEG_LEAN_X[i], lz = VEG_LEAN_Z[i];
                float t = VEG_TILT[i];
                /* up_unit: base -> apex direction (mostly +Y, leaning
                 * toward (lx, lz)). */
                float ux = lx * t, uy = 1.0f, uz = lz * t;
                float ulen = sqrtf(ux * ux + uy * uy + uz * uz);
                ux /= ulen; uy /= ulen; uz /= ulen;
                float h = VEG_HEIGHT[i];
                scn->cones[ci].apex = (vector){
                    VEG_BASE[i].x + ux * h,
                    VEG_BASE[i].y + uy * h,
                    VEG_BASE[i].z + uz * h,
                };
                scn->cones[ci].axis = (vector){-ux, -uy, -uz};
            }
        }

        Uint32 r0 = SDL_GetTicks();
        rt_renderer_render(rnd, scn, cam, &viewport, pixels, &gbuf);
        Uint32 r1 = SDL_GetTicks();

        /* Interlace leaves odd rows holding prior-frame content. Without
         * intervention, chromatic + grain compound on those stale rows
         * each frame and they drift into red/green static. Line-double
         * the rendered (even) rows down into the odd rows so postfx
         * operates on a fully-coherent image. The G-buffer needs the
         * same treatment because fog reads odd-row depth. Halves
         * vertical detail but kills the artifact entirely. */
        for (int y = 1; y < render_h; y += 2) {
            size_t row_px   = (size_t)render_w;
            memcpy(&pixels[y * render_w],         &pixels[(y - 1) * render_w],
                   row_px * sizeof(uint32_t));
            memcpy(&gbuf.depth[y * render_w],     &gbuf.depth[(y - 1) * render_w],
                   row_px * sizeof(float));
            memcpy(&gbuf.object_id[y * render_w], &gbuf.object_id[(y - 1) * render_w],
                   row_px * sizeof(uint32_t));
        }

        if (postfx_on) {
            postfx_fog_apply      (pixels, &(postfx_gbuffer){
                                       .object_id = gbuf.object_id,
                                       .depth     = gbuf.depth,
                                       .normal    = gbuf.normal,
                                   }, render_w, render_h, &fog_cfg);
            postfx_bloom_apply    (bloom, pixels, render_w, render_h, &bloom_cfg);
            postfx_chromatic_apply(chrom, pixels, render_w, render_h, &chrom_cfg);
            postfx_vignette_apply (pixels, render_w, render_h, &vig_cfg);
            grain_cfg.seed = frame_now;
            postfx_grain_apply    (pixels, render_w, render_h, &grain_cfg);
        }
        /* Wake-up fade: smooth ramp from pitch black on the first frame
         * to full brightness over WAKE_FADE_SEC. Smoothstep gives an
         * eyelids-opening feel — slow start, slow finish. Applied last
         * so postfx (grain, vignette) fades in too. */
        float wake_t = (frame_now - start_ticks) / 1000.0f / WAKE_FADE_SEC;
        if (wake_t < 1.0f) {
            if (wake_t < 0.0f) wake_t = 0.0f;
            float fade = wake_t * wake_t * (3.0f - 2.0f * wake_t);
            uint32_t mul = (uint32_t)(fade * 256.0f);
            int n = render_w * render_h;
            for (int i = 0; i < n; i++) {
                uint32_t p = pixels[i];
                uint32_t b = ((p        & 0xFF) * mul) >> 8;
                uint32_t g = (((p >> 8) & 0xFF) * mul) >> 8;
                uint32_t r = (((p >> 16) & 0xFF) * mul) >> 8;
                pixels[i] = (p & 0xFF000000) | (r << 16) | (g << 8) | b;
            }
        }

        /* Whispers — overlay last, so they fade in on top of the
         * already-postfx'd frame (and survive the wake fade naturally
         * because the first whisper fires well after it ends). */
        whisper_update(&whisper, dt, frame_now);
        whisper_render(&whisper, pixels, render_w, render_h);

        Uint32 fx1 = SDL_GetTicks();
        r_ms  += r1  - r0;
        fx_ms += fx1 - r1;

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float ar  = fps_frames ? (float)r_ms  / fps_frames : 0.0f;
            float afx = fps_frames ? (float)fx_ms / fps_frames : 0.0f;
            snprintf(title_buf, sizeof(title_buf),
                     "R'lyeh - %d FPS (rt=%.1fms fx=%.1fms) %dx%d %s",
                     fps_frames, ar, afx, render_w, render_h,
                     postfx_on ? "[postfx]" : "");
            SDL_SetWindowTitle(window, title_buf);
            fps_frames = 0; r_ms = 0; fx_ms = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    postfx_chromatic_destroy(chrom);
    postfx_bloom_destroy(bloom);
    free(gbuf.normal);
    free(gbuf.depth);
    free(gbuf.object_id);
    free(pixels);
    scene_camera_destroy(cam);
    scene_destroy(scn);
    rt_renderer_destroy(rnd);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    audio_shutdown();
    SDL_Quit();
    return 0;
}
