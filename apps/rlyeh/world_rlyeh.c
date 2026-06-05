/* R'lyeh — the dream world. A drowned alien plain ringed by obsidian
 * mountains under a bruised teal-purple sky, with leaning coral stalks and a
 * drifting Cthulhu silhouette. Pure scene: the SDL/GL/postfx harness lives in
 * main.c and drives this through the world_api at the bottom of the file. */

#include "world.h"
#include "renderer.h"   /* RT_OBJ_KIND_* for the fog skip mask */
#include "mesh.h"       /* rt_scene_build_accel */

#include <math.h>
#include <stdint.h>

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

/* ===== Animated handles ================================================== */

/* Cthulhu silhouette base + index. Filled at build time and read back each
 * frame; the in-scene sphere center is rewritten as base + offset so motion
 * never compounds. */
static vector CTHULHU_BASE;
static int    CTHULHU_IDX = -1;

/* Leaning coral stalks. The cone primitive stores apex + axis; to lean the
 * tip toward the camera while keeping the root planted, we keep each stalk's
 * BASE (rooted) position and its rest-tilt magnitude, and recompute apex +
 * axis per frame from the smoothed lean direction. The smoothing time-
 * constant is large (~25 s) so the motion reads as "watching" rather than
 * "tracking". Every cone in the scene leans — the scattered stalks and the
 * forward cluster alike — registered in add order via veg_register() so the
 * lean loop can walk them contiguously as VEG_FIRST_IDX + i. */
#define VEG_COUNT 21                   /* 14 scattered stalks + 7 cluster */
static int    VEG_FIRST_IDX = -1;
static int    VEG_REGISTERED = 0;      /* stalks registered for leaning so far */
static vector VEG_BASE  [VEG_COUNT];   /* root position (y ≈ 0) */
static float  VEG_HEIGHT[VEG_COUNT];
static float  VEG_TILT  [VEG_COUNT];   /* rest tilt magnitude (length of horizontal axis component) */
static float  VEG_LEAN_X[VEG_COUNT];   /* smoothed unit horizontal direction the tip leans toward */
static float  VEG_LEAN_Z[VEG_COUNT];
#define VEG_LEAN_RATE  0.04f           /* per-second smoothing factor (~25 s response) */

/* Register a freshly-added cone for the per-frame camera-lean. Cones must
 * be added to the scene contiguously (no non-stalk cones interleaved) so
 * the lean loop can address them as VEG_FIRST_IDX + i. Registrations past
 * VEG_COUNT are silently dropped. th seeds the rest lean direction. */
static void veg_register(int cone_idx, vector base, float height, float tilt,
                         float th) {
    if (VEG_REGISTERED >= VEG_COUNT) return;
    int v = VEG_REGISTERED++;
    if (VEG_FIRST_IDX < 0) VEG_FIRST_IDX = cone_idx;
    VEG_BASE  [v] = base;
    VEG_HEIGHT[v] = height;
    VEG_TILT  [v] = tilt;
    VEG_LEAN_X[v] = cosf(th);
    VEG_LEAN_Z[v] = sinf(th);
}

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
        /* Treat the root as approximately directly under the apex; with
         * tilt ≤ 0.18 the horizontal offset is well under 1 unit, which
         * the slow lean update absorbs harmlessly. */
        veg_register(idx, (vector){ clumps[i].x, 0.0f, clumps[i].z },
                     clumps[i].h, clumps[i].tilt, th);
    }
}

static void add_coral_cluster(scene *s, int stalk_mat, float cx, float cz) {
    /* A denser knot of stalks placed in one direction from spawn so the
     * eye is drawn to walk toward it. Position offsets are hand-picked
     * for asymmetry — never on a grid. Leans toward the camera like the
     * scattered stalks (registered below), so the whole knot cranes to
     * watch as you approach. */
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
        int idx = scene_add_cone(s, (scene_cone){
            .apex     = apex,
            .axis     = axis,
            .height   = cluster[i].h,
            .radius   = cluster[i].r,
            .material = stalk_mat,
        });
        veg_register(idx, (vector){ cx + cluster[i].dx, 0.0f, cz + cluster[i].dz },
                     cluster[i].h, cluster[i].tilt_az, th);
    }
}

/* ===== Build ============================================================= */
static void rlyeh_build(scene **out_s, scene_camera **out_cam,
                        int *out_sky_mat, postfx_fog *out_fog) {
    /* Reset our animated handles so a rebuild (e.g. after toggling away and
     * back) can't be read through indices into the previous scene. */
    CTHULHU_IDX = -1;
    VEG_FIRST_IDX = -1;
    VEG_REGISTERED = 0;

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
    /* Heightfield uses the per-cell colors baked into HF_COLORS, no
     * material needed. Pass -1 for the heightfield material to skip the
     * material-modulation path entirely. */

    /* Coral stalks — dark with cyan/teal marble veins, fully matte (no
     * reflections; the menacing cones read better dead-flat than glossy). */
    int m_stalk = scene_add_material(s, (scene_material){
        .albedo   = { 18,  35,  45},
        .albedo2  = { 70, 140, 150},
        .tex_kind = SCENE_TEX_MARBLE,
        .tex_scale = 1.2f,
        .reflectivity = 0.0f,
    });
    /* ===== Geometry ===== */
    /* Sky sphere — huge, centered on origin, gradient is along +Y.
     * Radius has to clear the mountain corners
     * (HF_WORLD_W * sqrt(2)/2 ≈ 1202). */
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

    /* Vegetation — stalks scattered across the plain, all leaning. */
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
        .reflectivity = 0.0f,              /* fully matte, like the cones */
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
    /* Fog targets the horizon teal so distant mountains fade into the sky
     * gradient. Skip sky + spheres (moons, Cthulhu) — they paint their own
     * backdrop colours. */
    if (out_fog) *out_fog = (postfx_fog){
        .enabled = 1, .color = { 14, 30, 44 },
        .start = 120.0f, .end = 700.0f, .max_strength = 1.00f,
        .skip_kinds_mask = (1u << RT_OBJ_KIND_SKY) | (1u << RT_OBJ_KIND_SPHERE),
    };
}

/* ===== Animate =========================================================== */
static void rlyeh_animate(scene *s, int sky_mat, float t_sec, float dt,
                          vector cam_pos, float wrongness, postfx_fog *fog) {
    (void)wrongness;   /* R'lyeh has no wrongness knob and owns no fog */
    (void)fog;

    /* Slow sky pulse — the atmosphere breathes. Cheap: two color triples
     * scaled by one multiplier per frame, no per-pixel cost. */
    if (sky_mat >= 0) {
        float pulse = SKY_PULSE_BIAS + SKY_PULSE_AMP * sinf(t_sec * SKY_PULSE_RATE);
        s->materials[sky_mat].albedo.r  = (uint8_t)(SKY_HORIZON_R * pulse);
        s->materials[sky_mat].albedo.g  = (uint8_t)(SKY_HORIZON_G * pulse);
        s->materials[sky_mat].albedo.b  = (uint8_t)(SKY_HORIZON_B * pulse);
        s->materials[sky_mat].albedo2.r = (uint8_t)(SKY_ZENITH_R  * pulse);
        s->materials[sky_mat].albedo2.g = (uint8_t)(SKY_ZENITH_G  * pulse);
        s->materials[sky_mat].albedo2.b = (uint8_t)(SKY_ZENITH_B  * pulse);
    }

    /* Cthulhu drift — slow lissajous in xz, gentler bob in y. Three slightly
     * off-rate sinusoids so the orbit never closes cleanly; the eye reads it
     * as "moving" without being able to pin down a path. */
    if (CTHULHU_IDX >= 0 && CTHULHU_IDX < s->sphere_count) {
        s->spheres[CTHULHU_IDX].center.x = CTHULHU_BASE.x + 38.0f * sinf(t_sec * 0.18f);
        s->spheres[CTHULHU_IDX].center.z = CTHULHU_BASE.z + 28.0f * cosf(t_sec * 0.13f);
        s->spheres[CTHULHU_IDX].center.y = CTHULHU_BASE.y + 12.0f * sinf(t_sec * 0.11f);
    }

    /* Coral stalks lean toward the camera with a long lag. Each frame:
     * compute target unit direction from base to camera (xz only), smooth
     * toward it at VEG_LEAN_RATE, then synthesise a fresh apex + axis pair so
     * the cone leans with that direction while the root stays planted. The
     * lerp is dt-scaled so the response is frame-rate-independent. */
    if (VEG_FIRST_IDX >= 0) {
        for (int i = 0; i < VEG_REGISTERED; i++) {
            int ci = VEG_FIRST_IDX + i;
            if (ci >= s->cone_count) break;
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
            /* up_unit: base -> apex direction (mostly +Y, leaning toward
             * (lx, lz)). */
            float ux = lx * t, uy = 1.0f, uz = lz * t;
            float ulen = sqrtf(ux * ux + uy * uy + uz * uz);
            ux /= ulen; uy /= ulen; uz /= ulen;
            float h = VEG_HEIGHT[i];
            s->cones[ci].apex = (vector){
                VEG_BASE[i].x + ux * h,
                VEG_BASE[i].y + uy * h,
                VEG_BASE[i].z + uz * h,
            };
            s->cones[ci].axis = (vector){-ux, -uy, -uz};
        }
    }
}

const world_api world_rlyeh = { "R'lyeh", rlyeh_build, rlyeh_animate };
