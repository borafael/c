#include "kit.h"

#include <math.h>
#include <stdint.h>

/* ---- Materials ---------------------------------------------------------- */

scene_color kit_hsv(float h, float s, float v) {
    h -= floorf(h);
    float r, g, b;
    float hh = h * 6.0f;
    int   i  = (int)hh;
    float f  = hh - (float)i;
    float p  = v * (1.0f - s);
    float q  = v * (1.0f - s * f);
    float u  = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
    case 0:  r = v; g = u; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = u; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = u; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return (scene_color){ (uint8_t)(r * 255.0f), (uint8_t)(g * 255.0f),
                          (uint8_t)(b * 255.0f) };
}

scene_material kit_matte(scene_color c) {
    scene_material m = scene_material_default();
    m.albedo = c;
    return m;
}

scene_material kit_glow(scene_color c) {
    scene_material m = kit_matte(c);
    m.unlit = 1;
    return m;
}

scene_material kit_tex(scene_color a, scene_color b,
                       scene_tex_kind kind, float scale) {
    scene_material m = scene_material_default();
    m.albedo    = a;
    m.albedo2   = b;
    m.tex_kind  = kind;
    m.tex_scale = scale;
    return m;
}

int kit_mat(scene *s, scene_material m) {
    return scene_add_material(s, m);
}

/* ---- Geometry ----------------------------------------------------------- */

vector kit_rotate(vector v, vector k, float a) {
    /* Rodrigues: v cosθ + (k × v) sinθ + k (k·v)(1 − cosθ) */
    float c = cosf(a), s = sinf(a);
    return vector_add(vector_add(vector_scale(v, c),
                                 vector_scale(vector_cross(k, v), s)),
                      vector_scale(k, vector_dot(k, v) * (1.0f - c)));
}

void kit_basis(vector n, vector *e1, vector *e2) {
    vector ref = (fabsf(n.y) < 0.9f) ? (vector){0.0f, 1.0f, 0.0f}
                                     : (vector){1.0f, 0.0f, 0.0f};
    *e1 = vector_normalize(vector_cross(n, ref));
    *e2 = vector_cross(n, *e1);
}

void kit_sphere(scene *s, vector c, float r, int mat) {
    scene_add_sphere(s, (scene_sphere){ .center = c, .radius = r,
                                        .material = mat });
}

kit_limb_end kit_add_tentacle(scene *s, const kit_tentacle *d, float t) {
    vector h = vector_normalize(d->dir);
    vector e1, e2;
    kit_basis(h, &e1, &e2);

    int   n    = d->segments > 0 ? d->segments : 1;
    float step = d->length / (float)n;
    vector p   = d->root;
    kit_sphere(s, p, d->r_root, d->mat);

    for (int i = 0; i < n; i++) {
        /* The "- i * k" phase makes the sway a wave travelling from root
         * to tip instead of the whole limb bending rigidly. */
        float w  = d->sway_freq * t + d->phase;
        float a1 = (d->curl + d->sway * sinf(w - (float)i * 0.55f)) / (float)n;
        float a2 = (d->sway * 0.6f * cosf(0.73f * w + 1.7f - (float)i * 0.4f))
                 / (float)n;

        h  = kit_rotate(h,  e2, a1);
        e1 = kit_rotate(e1, e2, a1);
        h  = kit_rotate(h,  e1, a2);
        e2 = kit_rotate(e2, e1, a2);
        h  = vector_normalize(h);

        vector q = vector_add(p, vector_scale(h, step));
        float  r = d->r_root + (d->r_tip - d->r_root) * (float)(i + 1) / (float)n;
        scene_add_cylinder(s, (scene_cylinder){
            .center      = vector_scale(vector_add(p, q), 0.5f),
            .axis        = h,
            .radius      = r,
            .half_height = step * 0.5f,
            .material    = d->mat });
        kit_sphere(s, q, r, d->mat);
        p = q;
    }
    return (kit_limb_end){ p, h };
}

/* ---- Eyes --------------------------------------------------------------- */

const kit_eye_style KIT_EYE_GOLD = {
    .sclera = 0xFFEBE1C8, .iris = 0xFFEBB450, .pupil = 0xFF0A0808,
    .iris_angle = 0.62f, .pupil_frac = 0.44f, .slit = 0.0f };
const kit_eye_style KIT_EYE_SLIT = {
    .sclera = 0xFF3A2010, .iris = 0xFFF0D030, .pupil = 0xFF050200,
    .iris_angle = 0.9f, .pupil_frac = 0.8f, .slit = 0.82f };
const kit_eye_style KIT_EYE_SHOGGOTH = {
    .sclera = 0xFFD8F0C0, .iris = 0xFF40E070, .pupil = 0xFF020A04,
    .iris_angle = 0.55f, .pupil_frac = 0.62f, .slit = 0.0f };
const kit_eye_style KIT_EYE_YITH = {
    .sclera = 0xFFD8C060, .iris = 0xFF201810, .pupil = 0xFF000000,
    .iris_angle = 0.32f, .pupil_frac = 0.5f, .slit = 0.0f };
const kit_eye_style KIT_EYE_LENS = {
    .sclera = 0xFF101014, .iris = 0xFFE02010, .pupil = 0xFFFFD860,
    .iris_angle = 0.7f, .pupil_frac = 0.3f, .slit = 0.0f };

#define EYE_W     128
#define EYE_H      64
#define EYE_POOL   48
static uint32_t eye_pool[EYE_POOL][EYE_W * EYE_H];
static int      eye_used = 0;

void kit_frame_begin(void) { eye_used = 0; }

static uint32_t argb_scale(uint32_t c, float k) {
    uint32_t r = (uint32_t)((float)((c >> 16) & 0xFF) * k);
    uint32_t g = (uint32_t)((float)((c >>  8) & 0xFF) * k);
    uint32_t b = (uint32_t)((float)( c        & 0xFF) * k);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

int kit_eye_texture(scene *s, const kit_eye_style *st,
                    const vector *looks, int n_looks) {
    if (eye_used >= EYE_POOL) return -1;
    if (n_looks > 4) n_looks = 4;
    uint32_t *px = eye_pool[eye_used++];

    /* Per-look tangent frame: R points "right" and U "up" as seen from
     * outside the eye, so a slit pupil stays vertical in the world. */
    vector L[4], R[4], U[4];
    for (int k = 0; k < n_looks; k++) {
        L[k] = vector_normalize(looks[k]);
        vector ref = (fabsf(L[k].y) < 0.95f) ? (vector){0.0f, 1.0f, 0.0f}
                                             : (vector){1.0f, 0.0f, 0.0f};
        R[k] = vector_normalize(vector_cross(ref, L[k]));
        U[k] = vector_cross(L[k], R[k]);
    }
    float iris_r  = sinf(st->iris_angle);
    float cos_lim = cosf(st->iris_angle);
    float pup_r   = iris_r * st->pupil_frac;
    float pup_rx  = pup_r * (1.0f - st->slit);
    float pup_ry  = st->slit > 0.0f ? iris_r * 0.92f : pup_r;
    float hl_r    = iris_r * 0.16f;

    for (int iy = 0; iy < EYE_H; iy++) {
        /* Inverse of the renderer's uv_sphere: v = acos(n.y)/π,
         * u = atan2(n.z, n.x)/2π + 0.5. */
        float theta = ((float)iy + 0.5f) / (float)EYE_H * (float)M_PI;
        float st_ = sinf(theta), ct = cosf(theta);
        for (int ix = 0; ix < EYE_W; ix++) {
            float phi = (((float)ix + 0.5f) / (float)EYE_W - 0.5f)
                      * 2.0f * (float)M_PI;
            vector n = { cosf(phi) * st_, ct, sinf(phi) * st_ };
            uint32_t c = st->sclera;
            for (int k = 0; k < n_looks; k++) {
                if (vector_dot(n, L[k]) < cos_lim) continue;
                float x = vector_dot(n, R[k]);
                float y = vector_dot(n, U[k]);
                float rr = sqrtf(x * x + y * y);
                /* Darker limbal ring toward the iris edge. */
                c = argb_scale(st->iris, rr > iris_r * 0.82f ? 0.55f : 1.0f);
                float px_ = x / pup_rx, py_ = y / pup_ry;
                if (px_ * px_ + py_ * py_ < 1.0f) c = st->pupil;
                float hx = x - iris_r * 0.35f, hy = y - iris_r * 0.35f;
                if (hx * hx + hy * hy < hl_r * hl_r) c = 0xFFFFFFFF;
                break;
            }
            px[iy * EYE_W + ix] = c;
        }
    }
    return scene_add_texture(s, (scene_texture){
        .pixels = px, .width = EYE_W, .height = EYE_H });
}

/* Cheap hash → [-1, 1]. Used to pick saccade targets. */
static float hash11(float x) {
    float s = sinf(x * 127.1f) * 43758.5453f;
    return (s - floorf(s)) * 2.0f - 1.0f;
}

int kit_eye(scene *s, vector c, float r, vector target,
            const kit_eye_style *st, int unlit, float t, float jitter) {
    vector look = vector_normalize(vector_sub(target, c));
    if (jitter > 0.0f) {
        /* Saccades: hold a random offset for a beat, then snap to a new
         * one. Seeded from position so neighbouring eyes disagree. */
        float seed = c.x * 3.1f + c.y * 5.7f + c.z * 7.3f;
        float beat = floorf(t * 0.9f + hash11(seed) * 3.0f);
        vector off = { hash11(beat + seed), hash11(beat * 1.7f + seed),
                       hash11(beat * 2.3f + seed) };
        look = vector_normalize(vector_add(look, vector_scale(off, jitter)));
    }
    int tex = kit_eye_texture(s, st, &look, 1);
    scene_material m = scene_material_default();
    m.unlit = unlit;
    if (tex >= 0) {
        m.tex_kind  = SCENE_TEX_IMAGE;
        m.tex_index = tex;
    } else {
        m.albedo = (scene_color){ (st->iris >> 16) & 0xFF,
                                  (st->iris >> 8) & 0xFF, st->iris & 0xFF };
    }
    int mat = kit_mat(s, m);
    kit_sphere(s, c, r, mat);
    return mat;
}

/* ---- Ophan ring tapestry (planar eyes, static) --------------------------- */

#define TAP_W 256
#define TAP_H 128
static uint32_t tapestry[TAP_W * TAP_H];
static int      tapestry_ready = 0;

static void paint_flat_eye(uint32_t *px, int W, int H,
                           float cx, float cy, float r) {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float dx = (float)x - cx, dy = (float)y - cy;
            float d = sqrtf(dx * dx + dy * dy);
            if (d >= r) continue;
            uint32_t c = KIT_EYE_GOLD.iris;
            if (d < r * 0.44f) c = KIT_EYE_GOLD.pupil;
            float hx = dx - 0.28f * r, hy = dy + 0.28f * r;
            if (hx * hx + hy * hy < (0.14f * r) * (0.14f * r)) c = 0xFFFFFFFF;
            px[y * W + x] = c;
        }
    }
}

int kit_tapestry_texture(scene *s) {
    if (!tapestry_ready) {
        for (int i = 0; i < TAP_W * TAP_H; i++) tapestry[i] = KIT_EYE_GOLD.sclera;
        float cw = TAP_W / 4.0f, ch = TAP_H / 2.0f;
        for (int gy = 0; gy < 2; gy++)
            for (int gx = 0; gx < 4; gx++)
                paint_flat_eye(tapestry, TAP_W, TAP_H,
                               (gx + 0.5f) * cw, (gy + 0.5f) * ch, 0.38f * ch);
        tapestry_ready = 1;
    }
    return scene_add_texture(s, (scene_texture){
        .pixels = tapestry, .width = TAP_W, .height = TAP_H });
}
