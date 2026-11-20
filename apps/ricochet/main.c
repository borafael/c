/* Ricochet: a first-person shooter where everything is a sphere.
 *
 * Enemies are rolling spheres with one glowing eye that hunt you across a
 * walled arena. Your machine gun sprays little glowing spheres that really
 * collide: they hop off enemies (shoving them), off the floor, walls,
 * pillars and each other, then settle. You only carry MAX_AMMO of them, so once
 * they're spent you must walk over them to pick them back up. A ball
 * at rest pulses brighter and dimmer so you can find it.
 *
 * Every sound is synthesized at startup (see sfx.c) and panned and
 * faded by distance relative to the player:
 *   shoot   fired          hit     ball strikes an enemy hard enough to hurt
 *   bounce  ball on floor  wall    ball on wall / pillar / other ball
 *   pickup, pop (enemy destroyed), hurt, dry (empty trigger)
 *
 * Controls
 *   WASD move   mouse look (arrows fallback)   hold LMB / SPACE to fire
 *   SHIFT sprint   M toggle mouse capture   R restart
 *   V cycle render resolution (- / = step down / up)   B bloom
 *   TAB CPU/OpenGL   F11 fullscreen   ESC quit
 *
 * Starts on the OpenGL compute backend when the driver has GL 4.3
 * (Linux / Windows), otherwise on the CPU tracer; --cpu forces the CPU.
 *
 * Flags: --fullscreen (start fullscreen), --no-reflections (matte surfaces,
 * roughly 40% cheaper on the CPU tracer).
 *
 * Flags: --bot (autopilot plays), --shot out.ppm [--sim N] (headless:
 * the bot plays N seconds, renders one frame and prints sound-event
 * counts), --dump-sfx DIR (write every sound as a WAV), --seed N. */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "postfx.h"
#include "hud.h"
#include "sfx.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 600
#define FOV ((float)M_PI / 2.6f)

/* Render-resolution ladder, by height; the width follows the window's
 * aspect so nothing stretches windowed or fullscreen. 0 = native window
 * size. Rungs taller than the window are skipped. V cycles, -/= step. */
static const int RES_HEIGHTS[] = {150, 200, 240, 300, 400, 600, 0};
#define RES_N           ((int)(sizeof RES_HEIGHTS / sizeof RES_HEIGHTS[0]))
#define RES_DEFAULT_IDX 0   /* 150 rows: chunky, cheapest; same cost windowed or fullscreen */

static int res_usable(int idx, int window_h) {
    return RES_HEIGHTS[idx] == 0 || RES_HEIGHTS[idx] < window_h;
}

/* Next usable rung in direction dir (+1 / -1). wrap: cycle past the ends. */
static int res_step(int idx, int dir, int wrap, int window_h) {
    for (int k = 0; k < RES_N; k++) {
        int n = idx + dir;
        if (n < 0 || n >= RES_N) {
            if (!wrap) return idx;
            n = (n + RES_N) % RES_N;
        }
        idx = n;
        if (res_usable(idx, window_h)) return idx;
    }
    return idx;
}

static void res_size(int idx, int window_w, int window_h, int *w, int *h) {
    int rh = RES_HEIGHTS[idx];
    if (rh == 0 || rh > window_h) rh = window_h;
    int rw = (int)((float)rh * (float)window_w / (float)window_h + 0.5f);
    *w = rw < 1 ? 1 : rw;
    *h = rh < 1 ? 1 : rh;
}

/* Arena */
#define ARENA_HALF   16.0f
#define WALL_H        2.6f
#define WALL_T        0.5f
#define PILLAR_H      9.0f

/* Player */
#define EYE_H         1.6f
#define PLAYER_R      0.45f
#define WALK_SPEED    6.0f
#define SPRINT_SPEED  9.0f
#define MOUSE_SENS    0.0024f
#define LOOK_SPEED    2.2f
#define PITCH_LIMIT   1.40f
#define PLAYER_HP     100.0f
#define INVULN_TIME   0.9f

/* Ammo / balls */
#define MAX_AMMO      40
#define BALL_R        0.13f
#define SHOOT_SPEED   110.0f   /* fast enough that gravity drop is ~0.14 m
                                * at 15 m and ~0.56 m across the arena */
#define FIRE_COOLDOWN 0.07f    /* ~14 rounds/s while the trigger is held */
#define DRY_COOLDOWN  0.30f    /* empty-click repeat while held */
#define SPREAD        0.018f   /* radians of random cone per shot */
#define GRAVITY       15.0f
#define REST_FLOOR    0.32f    /* vertical restitution on the floor */
#define FLOOR_GRIP    0.70f    /* horizontal speed kept per floor impact */
#define SETTLE_SPEED  1.6f     /* floor impacts slower than this stop dead */
#define REST_WALL     0.15f    /* walls soak up most of a hard shot */
#define REST_ENEMY    0.12f
#define REST_BALL     0.40f
#define ROLL_FRICTION 3.5f     /* 1/s, horizontal damping while rolling */
#define PICKUP_R      1.05f    /* horizontal reach to grab a ball */
#define PICKUP_MIN_AGE 0.35f   /* don't instantly re-grab the one you fired */
#define HIT_MIN_SPEED 4.0f     /* normal impact speed that counts as damage */
#define MAX_SUBSTEP_DIST 0.08f /* m a ball may travel per physics substep */
#define MAX_SUBSTEPS  64

/* Enemies */
#define MAX_ENEMIES   24
#define SPAWN_TIME    0.9f     /* rise out of the floor */
#define DIE_TIME      0.30f
#define ENEMY_ACCEL   7.0f
#define KNOCKBACK     0.6f     /* scales momentum transfer for feel */
#define WAVE_GAP      2.5f

#define MAX_PARTS     160

/* ------------------------------------------------------------------ RNG */

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rng_u32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 32);
}
static float frand(void) { return (rng_u32() >> 8) * (1.0f / 16777216.0f); }
static float frange(float a, float b) { return a + (b - a) * frand(); }

/* ---------------------------------------------------------------- World */

typedef struct { float x, z, r; } pillar;
static const pillar PILLARS[] = {
    {-7.0f, -7.0f, 0.9f}, { 7.0f, -7.0f, 0.9f},
    {-7.0f,  7.0f, 0.9f}, { 7.0f,  7.0f, 0.9f},
    { 0.0f, -11.0f, 0.6f}, { 0.0f, 11.0f, 0.6f},
    {-11.0f, 0.0f, 0.6f}, { 11.0f, 0.0f, 0.6f},
};
#define N_PILLARS ((int)(sizeof PILLARS / sizeof PILLARS[0]))

typedef enum { EN_SMALL, EN_MED, EN_BIG, EN_KINDS } en_kind;
typedef struct {
    float r, speed, mass, damage;
    int   hp, score;
    scene_color c1, c2;
} en_spec;
static const en_spec SPECS[EN_KINDS] = {
    {0.55f, 4.2f,  5.0f, 10.0f,  3, 100, {255, 160,  40}, {120,  50,  10}},
    {0.85f, 2.8f, 14.0f, 20.0f,  7, 250, {230,  45,  80}, { 90,  10,  30}},
    {1.30f, 1.8f, 40.0f, 30.0f, 16, 600, {150,  70, 230}, { 45,  15,  80}},
};

typedef struct {
    int    alive;
    vector p, v;
    float  age;
    int    resting;
} ball;

typedef struct {
    int    alive;
    int    kind;
    vector p, v;        /* p.y is the center height */
    vector face;        /* unit, horizontal look direction of the eye */
    int    hp;
    float  flash;       /* 1 on hit, decays */
    float  spawn_t;     /* <0 waiting, 0..SPAWN_TIME rising, then active */
    float  dying;       /* 0 = alive, >0 seconds into death */
} enemy;

typedef struct {
    int    alive;
    vector p, v;
    float  life, max_life, r;
    scene_color c;
} particle;

typedef struct {
    vector p, v;        /* p.y ignored (feet on the floor) */
    float  yaw, pitch;
    float  hp, invuln, hurt_flash;
    float  fire_cd, recoil, bob;
    float  heat;        /* 0..1, rises while firing; glows the barrel rings */
} player;

typedef struct {
    float move_f, move_s;  /* -1..1 */
    int   sprint;
    int   fire;            /* trigger held this frame (automatic fire) */
    float look_dx, look_dy;/* radians */
} input;

typedef struct {
    player   pl;
    ball     balls[MAX_AMMO];
    int      ammo;               /* balls in hand */
    enemy    en[MAX_ENEMIES];
    particle parts[MAX_PARTS];
    int      wave, kills, score, shots, hits;
    float    wave_timer;         /* countdown to next wave when > 0 */
    float    banner_t;
    char     banner[48];
    int      game_over;
    float    t;
    /* Listener frame for spatial audio (updated each frame). */
    vector   ear, ear_right;
} game;

static vector cam_forward(float yaw, float pitch) {
    return (vector){cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)};
}

static vector eye_pos(const game *g) {
    return (vector){g->pl.p.x, EYE_H + 0.04f * sinf(g->pl.bob), g->pl.p.z};
}

/* Gun-local frame: origin at the receiver, x = right, y = up, z = forward
 * (the camera basis). Recoil pushes it back and up, walking sways it. The
 * viewmodel and the projectile spawn both use it so balls leave the
 * actual muzzle. */
typedef struct { vector o, x, y, z; } gun_frame;

#define GUN_MUZZLE_Z 0.42f
#define GUN_MUZZLE_Y 0.012f

static gun_frame gun_frame_make(const game *g, vector eye, vector f, vector r, vector u) {
    float kick = g->pl.recoil;
    float sx = 0.008f * sinf(g->pl.bob);
    float sy = 0.006f * fabsf(cosf(g->pl.bob));
    vector o = vector_add(eye, vector_scale(f, 0.56f - 0.035f * kick));
    o = vector_add(o, vector_scale(r, 0.19f + sx));
    o = vector_add(o, vector_scale(u, -0.19f + sy + 0.010f * kick));
    return (gun_frame){o, r, u, f};
}

static vector gun_local(const gun_frame *gf, float x, float y, float z) {
    return vector_add(gf->o, vector_add(vector_scale(gf->x, x),
                      vector_add(vector_scale(gf->y, y), vector_scale(gf->z, z))));
}

/* --------------------------------------------------------------- Audio */

static void play_at(game *g, sfx_id id, vector at, float gain, float rate) {
    vector d = vector_sub(at, g->ear);
    float dist = vector_magnitude(d);
    float att = 1.0f / (1.0f + 0.10f * dist);
    float pan = dist > 0.01f ? vector_dot(vector_scale(d, 1.0f / dist), g->ear_right) * 0.8f : 0.0f;
    sfx_play(id, gain * att, pan, rate);
}

/* Feature toggles used by --bench to attribute render cost. */
/* no_reflections is also the --no-reflections flag (all surfaces matte). */
static int no_reflections, bench_no_gun, bench_no_parts;

/* ------------------------------------------------------------- Helpers */

static void banner(game *g, const char *s) {
    snprintf(g->banner, sizeof g->banner, "%s", s);
    g->banner_t = 2.2f;
}

static int enemies_left(const game *g) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g->en[i].alive && g->en[i].dying == 0.0f) n++;
    return n;
}

static float enemy_radius(const enemy *e) {
    float r = SPECS[e->kind].r;
    if (e->dying > 0.0f) r *= 1.0f + 0.35f * (e->dying / DIE_TIME);
    return r;
}

/* Push a circle (xz) out of pillars. Returns the pillar normal hit, or
 * a zero vector if no contact. */
static vector push_out_pillars(vector *p, float r) {
    vector hit = {0, 0, 0};
    for (int i = 0; i < N_PILLARS; i++) {
        float dx = p->x - PILLARS[i].x, dz = p->z - PILLARS[i].z;
        float d = sqrtf(dx * dx + dz * dz), min = PILLARS[i].r + r;
        if (d < min && d > 1e-5f) {
            float nx = dx / d, nz = dz / d;
            p->x = PILLARS[i].x + nx * min;
            p->z = PILLARS[i].z + nz * min;
            hit = (vector){nx, 0, nz};
        }
    }
    return hit;
}

static void clamp_arena(vector *p, float r) {
    float lim = ARENA_HALF - r;
    if (p->x >  lim) p->x =  lim;
    if (p->x < -lim) p->x = -lim;
    if (p->z >  lim) p->z =  lim;
    if (p->z < -lim) p->z = -lim;
}

static void burst(game *g, vector at, scene_color c, int n, float speed) {
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < MAX_PARTS; i++) {
            if (g->parts[i].alive) continue;
            vector dir = vector_normalize((vector){frange(-1, 1), frange(0.2f, 1.4f), frange(-1, 1)});
            float life = frange(0.5f, 1.1f);
            g->parts[i] = (particle){1, at, vector_scale(dir, speed * frange(0.4f, 1.0f)),
                                     life, life, frange(0.05f, 0.11f), c};
            break;
        }
    }
}

/* ----------------------------------------------------------- Game flow */

static void spawn_wave(game *g) {
    g->wave++;
    int count = 2 + g->wave;
    if (count > MAX_ENEMIES) count = MAX_ENEMIES;
    float big_p = fminf(0.08f * (float)(g->wave - 1), 0.35f);
    int placed = 0;
    for (int i = 0; i < MAX_ENEMIES && placed < count; i++) {
        if (g->en[i].alive) continue;
        float roll = frand();
        int kind = roll < big_p ? EN_BIG : (roll < big_p + 0.45f ? EN_SMALL : EN_MED);
        float r = SPECS[kind].r;
        vector p = {0, r, 0};
        for (int tries = 0; tries < 100; tries++) {
            p.x = frange(-ARENA_HALF + r + 1, ARENA_HALF - r - 1);
            p.z = frange(-ARENA_HALF + r + 1, ARENA_HALF - r - 1);
            float dx = p.x - g->pl.p.x, dz = p.z - g->pl.p.z;
            if (dx * dx + dz * dz < 11.0f * 11.0f) continue;
            vector q = p;
            vector hit = push_out_pillars(&q, r + 0.6f);
            if (hit.x != 0 || hit.z != 0) continue;
            break;
        }
        g->en[i] = (enemy){
            .alive = 1, .kind = kind, .p = p, .v = {0, 0, 0},
            .face = vector_normalize((vector){g->pl.p.x - p.x, 0, g->pl.p.z - p.z}),
            .hp = SPECS[kind].hp,
            .spawn_t = -0.4f * (float)placed,
        };
        placed++;
    }
    char buf[32];
    snprintf(buf, sizeof buf, "WAVE %d", g->wave);
    banner(g, buf);
}

static void game_reset(game *g) {
    memset(g, 0, sizeof *g);
    g->pl.p = (vector){0, 0, -3.0f};
    g->pl.hp = PLAYER_HP;
    g->ammo = MAX_AMMO;
    g->wave_timer = 1.0f;
}

static void enemy_damage(game *g, enemy *e, vector at) {
    e->flash = 1.0f;
    if (--e->hp > 0) return;
    e->dying = 1e-4f;
    g->kills++;
    g->score += SPECS[e->kind].score * (g->wave > 0 ? g->wave : 1);
    play_at(g, SFX_POP, at, 1.0f, 1.25f - 0.25f * SPECS[e->kind].r);
    burst(g, e->p, SPECS[e->kind].c1, 14 + 6 * e->kind, 7.0f);
}

/* ------------------------------------------------------------- Physics */

static void ball_vs_wall(game *g, ball *b, float *pc, float *vc, float lim, int sign) {
    if (sign * *pc <= lim) return;
    *pc = sign * lim;
    if (sign * *vc > 0) {
        float imp = fabsf(*vc);
        *vc = -*vc * REST_WALL;
        if (imp > 1.2f) play_at(g, SFX_WALL, b->p, fminf(1.0f, imp / 18.0f), frange(0.9f, 1.1f));
    }
}

static void step_balls(game *g, float h) {
    for (int i = 0; i < MAX_AMMO; i++) {
        ball *b = &g->balls[i];
        if (!b->alive) continue;
        b->age += h;
        if (b->resting) continue;

        b->v.y -= GRAVITY * h;
        b->p = vector_add(b->p, vector_scale(b->v, h));

        /* Floor. */
        if (b->p.y < BALL_R) {
            b->p.y = BALL_R;
            if (b->v.y < 0) {
                float imp = -b->v.y;
                if (imp > 1.0f)
                    play_at(g, SFX_BOUNCE, b->p, fminf(1.0f, 0.15f + imp / 14.0f),
                            frange(0.92f, 1.08f) + fminf(imp, 12.0f) * 0.01f);
                b->v.y = imp > SETTLE_SPEED ? imp * REST_FLOOR : 0.0f;
                b->v.x *= FLOOR_GRIP;
                b->v.z *= FLOOR_GRIP;
            }
        }
        if (b->p.y <= BALL_R + 1e-4f && b->v.y == 0.0f) {
            float k = expf(-ROLL_FRICTION * h);
            b->v.x *= k;
            b->v.z *= k;
            if (b->v.x * b->v.x + b->v.z * b->v.z < 0.10f * 0.10f) {
                b->v = (vector){0, 0, 0};
                b->resting = 1;
            }
        }

        /* Arena walls (physically infinitely tall, so nothing escapes). */
        float lim = ARENA_HALF - BALL_R;
        ball_vs_wall(g, b, &b->p.x, &b->v.x, lim,  1);
        ball_vs_wall(g, b, &b->p.x, &b->v.x, lim, -1);
        ball_vs_wall(g, b, &b->p.z, &b->v.z, lim,  1);
        ball_vs_wall(g, b, &b->p.z, &b->v.z, lim, -1);

        /* Pillars. */
        vector n = push_out_pillars(&b->p, BALL_R);
        if (n.x != 0 || n.z != 0) {
            float vn = b->v.x * n.x + b->v.z * n.z;
            if (vn < 0) {
                b->v.x -= (1.0f + REST_WALL) * vn * n.x;
                b->v.z -= (1.0f + REST_WALL) * vn * n.z;
                if (-vn > 1.2f) play_at(g, SFX_WALL, b->p, fminf(1.0f, -vn / 18.0f), frange(0.75f, 0.85f));
            }
        }

        /* Enemies: impulse response between a light ball and a heavy,
         * ground-bound sphere. */
        for (int e = 0; e < MAX_ENEMIES; e++) {
            enemy *en = &g->en[e];
            if (!en->alive || en->spawn_t < 0.0f) continue;
            float er = enemy_radius(en);
            vector d = vector_sub(b->p, en->p);
            float dist = vector_magnitude(d), min = er + BALL_R;
            if (dist >= min || dist < 1e-5f) continue;
            vector nn = vector_scale(d, 1.0f / dist);
            b->p = vector_add(en->p, vector_scale(nn, min));
            vector vrel = vector_sub(b->v, en->v);
            float vn = vector_dot(vrel, nn);
            if (vn >= 0) continue;
            float me = SPECS[en->kind].mass;
            float j = -(1.0f + REST_ENEMY) * vn / (1.0f + 1.0f / me);
            b->v = vector_add(b->v, vector_scale(nn, j));
            vector kick = vector_scale(nn, -j / me * KNOCKBACK);
            en->v.x += kick.x;
            en->v.z += kick.z;
            b->resting = 0;
            float imp = -vn;
            int can_hurt = en->dying == 0.0f && en->spawn_t >= SPAWN_TIME;
            if (imp > HIT_MIN_SPEED && can_hurt) {
                g->hits++;
                play_at(g, SFX_HIT, b->p, fminf(1.0f, 0.35f + imp / 25.0f),
                        1.3f - 0.35f * SPECS[en->kind].r + frange(-0.04f, 0.04f));
                burst(g, b->p, (scene_color){200, 250, 255}, 4, 3.0f);
                enemy_damage(g, en, b->p);
            } else if (imp > 1.2f) {
                play_at(g, SFX_WALL, b->p, fminf(0.7f, imp / 12.0f), 1.3f);
            }
        }
    }

    /* Ball-ball: equal masses. */
    for (int i = 0; i < MAX_AMMO; i++) {
        ball *a = &g->balls[i];
        if (!a->alive) continue;
        for (int k = i + 1; k < MAX_AMMO; k++) {
            ball *b = &g->balls[k];
            if (!b->alive) continue;
            vector d = vector_sub(b->p, a->p);
            float dist = vector_magnitude(d);
            if (dist >= 2 * BALL_R || dist < 1e-5f) continue;
            vector n = vector_scale(d, 1.0f / dist);
            float push = (2 * BALL_R - dist) * 0.5f;
            a->p = vector_sub(a->p, vector_scale(n, push));
            b->p = vector_add(b->p, vector_scale(n, push));
            float vn = vector_dot(vector_sub(b->v, a->v), n);
            if (vn >= 0) continue;
            float j = -(1.0f + REST_BALL) * vn * 0.5f;
            a->v = vector_sub(a->v, vector_scale(n, j));
            b->v = vector_add(b->v, vector_scale(n, j));
            a->resting = b->resting = 0;
            if (-vn > 1.0f) play_at(g, SFX_WALL, b->p, fminf(0.8f, -vn / 14.0f), 1.9f);
        }
    }
}

static void update_enemies(game *g, float dt) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy *e = &g->en[i];
        if (!e->alive) continue;
        const en_spec *s = &SPECS[e->kind];
        e->flash = fmaxf(0.0f, e->flash - dt * 6.0f);

        if (e->dying > 0.0f) {
            e->dying += dt;
            if (e->dying >= DIE_TIME) {
                e->alive = 0;
                burst(g, e->p, s->c2, 10, 5.0f);
            }
            continue;
        }
        if (e->spawn_t < SPAWN_TIME) {
            e->spawn_t += dt;
            float k = fmaxf(0.0f, e->spawn_t) / SPAWN_TIME;
            if (k > 1) k = 1;
            k = 1 - (1 - k) * (1 - k);
            e->p.y = -s->r + 2.0f * s->r * k;
            continue;
        }
        e->p.y = s->r;

        vector to = {g->pl.p.x - e->p.x, 0, g->pl.p.z - e->p.z};
        float dist = sqrtf(to.x * to.x + to.z * to.z);
        vector dir = dist > 1e-4f ? vector_scale(to, 1.0f / dist) : (vector){0, 0, 1};

        /* Slight weave so a crowd doesn't move in lockstep. */
        float weave = 0.35f * sinf(g->t * 1.3f + (float)i * 2.1f);
        vector side = {-dir.z, 0, dir.x};
        vector want = vector_normalize(vector_add(dir, vector_scale(side, weave)));
        float speed = s->speed * (1.0f + 0.04f * (float)(g->wave - 1));
        if (g->game_over) speed *= 0.3f;
        want = vector_scale(want, speed);

        vector dv = vector_sub(want, e->v);
        dv.y = 0;
        float dvl = vector_magnitude(dv), maxdv = ENEMY_ACCEL * dt;
        if (dvl > maxdv) dv = vector_scale(dv, maxdv / dvl);
        e->v = vector_add(e->v, dv);
        e->v.y = 0;
        e->p = vector_add(e->p, vector_scale(e->v, dt));

        /* Walls / pillars: bounce off a little so knockback reads. */
        float lim = ARENA_HALF - s->r;
        if (e->p.x >  lim) { e->p.x =  lim; e->v.x = -fabsf(e->v.x) * 0.4f; }
        if (e->p.x < -lim) { e->p.x = -lim; e->v.x =  fabsf(e->v.x) * 0.4f; }
        if (e->p.z >  lim) { e->p.z =  lim; e->v.z = -fabsf(e->v.z) * 0.4f; }
        if (e->p.z < -lim) { e->p.z = -lim; e->v.z =  fabsf(e->v.z) * 0.4f; }
        vector n = push_out_pillars(&e->p, s->r);
        if (n.x != 0 || n.z != 0) {
            float vn = e->v.x * n.x + e->v.z * n.z;
            if (vn < 0) { e->v.x -= vn * n.x; e->v.z -= vn * n.z; }   /* slide */
        }

        /* Separate from other enemies. */
        for (int k = 0; k < MAX_ENEMIES; k++) {
            enemy *o = &g->en[k];
            if (k == i || !o->alive || o->dying > 0 || o->spawn_t < SPAWN_TIME) continue;
            float dx = e->p.x - o->p.x, dz = e->p.z - o->p.z;
            float d = sqrtf(dx * dx + dz * dz), min = s->r + SPECS[o->kind].r;
            if (d < min && d > 1e-4f) {
                float push = (min - d) * 0.5f;
                e->p.x += dx / d * push;
                e->p.z += dz / d * push;
            }
        }

        /* Eye tracks the player smoothly. */
        e->face = vector_normalize(vector_add(e->face, vector_scale(dir, dt * 5.0f)));

        /* Contact damage. */
        if (!g->game_over && dist < s->r + PLAYER_R && g->pl.invuln <= 0.0f) {
            g->pl.hp -= s->damage;
            g->pl.invuln = INVULN_TIME;
            g->pl.hurt_flash = 1.0f;
            g->pl.v = vector_add(g->pl.v, vector_scale(dir, 9.0f));
            e->v = vector_scale(dir, -4.0f);
            sfx_play(SFX_HURT, 0.9f, 0.0f, 1.0f);
            if (g->pl.hp <= 0) {
                g->pl.hp = 0;
                g->game_over = 1;
                banner(g, "GAME OVER");
                g->banner_t = 1e9f;
            }
        }
    }
}

static void update_particles(game *g, float dt) {
    for (int i = 0; i < MAX_PARTS; i++) {
        particle *p = &g->parts[i];
        if (!p->alive) continue;
        p->life -= dt;
        if (p->life <= 0) { p->alive = 0; continue; }
        p->v.y -= GRAVITY * 0.7f * dt;
        p->p = vector_add(p->p, vector_scale(p->v, dt));
        if (p->p.y < p->r) { p->p.y = p->r; p->v.y = fabsf(p->v.y) * 0.4f; p->v.x *= 0.7f; p->v.z *= 0.7f; }
    }
}

static void fire(game *g) {
    player *pl = &g->pl;
    if (pl->fire_cd > 0.0f || g->game_over) return;
    pl->fire_cd = FIRE_COOLDOWN;
    if (g->ammo <= 0) {
        pl->fire_cd = DRY_COOLDOWN;
        sfx_play(SFX_DRY, 0.7f, 0.0f, frange(0.95f, 1.05f));
        return;
    }
    int slot = -1;
    for (int i = 0; i < MAX_AMMO; i++) if (!g->balls[i].alive) { slot = i; break; }
    if (slot < 0) return;

    vector eye = eye_pos(g);
    vector fwd = cam_forward(pl->yaw, pl->pitch);
    vector right = vector_normalize(vector_cross(fwd, (vector){0, 1, 0}));
    if (vector_dot(right, g->ear_right) < 0) right = vector_scale(right, -1.0f);
    vector up = vector_cross(right, fwd);
    if (up.y < 0) up = vector_scale(up, -1.0f);
    /* Fire from the rifle's muzzle, converging on the crosshair 25 m out.
     * If the muzzle is poking into a pillar, spawn just in front of the
     * eye instead so the ball can't tunnel out the far side. */
    gun_frame gf = gun_frame_make(g, eye, fwd, right, up);
    vector muzzle = gun_local(&gf, 0.0f, GUN_MUZZLE_Y, GUN_MUZZLE_Z + 0.03f);
    vector probe = muzzle;
    vector hit = push_out_pillars(&probe, BALL_R);
    if (hit.x != 0 || hit.z != 0) muzzle = vector_add(eye, vector_scale(fwd, 0.3f));
    vector target = vector_add(eye, vector_scale(fwd, 25.0f));
    vector dir = vector_normalize(vector_sub(target, muzzle));
    dir = vector_normalize(vector_add(dir, vector_add(vector_scale(right, frange(-SPREAD, SPREAD)),
                                                      vector_scale(up, frange(-SPREAD, SPREAD)))));
    vector v = vector_add(vector_scale(dir, SHOOT_SPEED), vector_scale(pl->v, 0.5f));

    g->balls[slot] = (ball){1, muzzle, v, 0.0f, 0};
    g->ammo--;
    g->shots++;
    pl->recoil = 1.0f;
    pl->heat = fminf(1.0f, pl->heat + 0.045f);
    /* Rapid fire: quieter, shorter-feeling and pitch-jittered so a burst
     * reads as a rattle rather than one droning tone. */
    sfx_play(SFX_SHOOT, 0.55f, 0.0f, frange(1.05f, 1.25f));
}

static void update_player(game *g, const input *in, float dt) {
    player *pl = &g->pl;
    pl->yaw += in->look_dx;
    pl->pitch += in->look_dy;
    if (pl->pitch >  PITCH_LIMIT) pl->pitch =  PITCH_LIMIT;
    if (pl->pitch < -PITCH_LIMIT) pl->pitch = -PITCH_LIMIT;

    pl->fire_cd = fmaxf(0.0f, pl->fire_cd - dt);
    pl->invuln = fmaxf(0.0f, pl->invuln - dt);
    pl->hurt_flash = fmaxf(0.0f, pl->hurt_flash - dt * 2.5f);
    pl->recoil = fmaxf(0.0f, pl->recoil - dt * 7.0f);
    pl->heat = fmaxf(0.0f, pl->heat - dt * 0.30f);

    vector fwd = {sinf(pl->yaw), 0, cosf(pl->yaw)};
    vector right = {g->ear_right.x, 0, g->ear_right.z};
    right = vector_magnitude(right) > 1e-4f ? vector_normalize(right) : (vector){1, 0, 0};
    vector wish = vector_add(vector_scale(fwd, in->move_f), vector_scale(right, in->move_s));
    float wl = vector_magnitude(wish);
    if (wl > 1.0f) wish = vector_scale(wish, 1.0f / wl);
    if (g->game_over) wish = (vector){0, 0, 0};
    float speed = in->sprint ? SPRINT_SPEED : WALK_SPEED;
    vector target = vector_scale(wish, speed);
    float k = 1.0f - expf(-dt * 10.0f);
    pl->v.x += (target.x - pl->v.x) * k;
    pl->v.z += (target.z - pl->v.z) * k;
    pl->v.y = 0;
    pl->p = vector_add(pl->p, vector_scale(pl->v, dt));
    clamp_arena(&pl->p, PLAYER_R);
    push_out_pillars(&pl->p, PLAYER_R);
    float hs = sqrtf(pl->v.x * pl->v.x + pl->v.z * pl->v.z);
    pl->bob += hs * dt * 1.6f;

    if (in->fire) fire(g);

    /* Recollect: walk over (or catch) a ball. */
    for (int i = 0; i < MAX_AMMO; i++) {
        ball *b = &g->balls[i];
        if (!b->alive || b->age < PICKUP_MIN_AGE || b->p.y > 2.2f) continue;
        float dx = b->p.x - pl->p.x, dz = b->p.z - pl->p.z;
        if (dx * dx + dz * dz > PICKUP_R * PICKUP_R) continue;
        b->alive = 0;
        g->ammo++;
        sfx_play(SFX_PICKUP, 0.45f, 0.0f, 0.9f + 0.6f * (float)g->ammo / MAX_AMMO);
    }
}

static void game_update(game *g, const input *in, float dt) {
    g->t += dt;
    g->banner_t -= dt;
    update_player(g, in, dt);
    /* Substep count follows the fastest ball so nothing moves more than
     * MAX_SUBSTEP_DIST per step: fast shots can't tunnel through enemies
     * or each other, and a field of resting balls stays cheap. */
    float vmax = 0.0f;
    for (int i = 0; i < MAX_AMMO; i++)
        if (g->balls[i].alive && !g->balls[i].resting)
            vmax = fmaxf(vmax, vector_magnitude(g->balls[i].v));
    int steps = (int)ceilf(vmax * dt / MAX_SUBSTEP_DIST);
    if (steps < 4) steps = 4;
    if (steps > MAX_SUBSTEPS) steps = MAX_SUBSTEPS;
    float h = dt / (float)steps;
    for (int s = 0; s < steps; s++) step_balls(g, h);
    update_enemies(g, dt);
    update_particles(g, dt);

    if (!g->game_over) {
        if (g->wave_timer > 0.0f) {
            g->wave_timer -= dt;
            if (g->wave_timer <= 0.0f) spawn_wave(g);
        } else if (enemies_left(g) == 0) {
            g->wave_timer = WAVE_GAP;
            if (g->wave > 0) banner(g, "WAVE CLEAR");
        }
    }
}

/* ------------------------------------------------------------------ Bot */

/* Autopilot: aims (with gravity drop compensation) at the nearest enemy,
 * fires short bursts, and goes hunting for balls once the magazine runs
 * low (with hysteresis so it refills properly before fighting again). */
static void bot_input(const game *g, input *in, float dt) {
    static float burst_clock = 0.0f;
    static int collecting = 0;
    memset(in, 0, sizeof *in);
    burst_clock += dt;
    const player *pl = &g->pl;
    vector eye = eye_pos(g);

    int out_in_world = MAX_AMMO - g->ammo;
    if (g->ammo == 0 || (enemies_left(g) == 0 && out_in_world > 0)) collecting = 1;
    if (g->ammo >= MAX_AMMO * 3 / 4 || out_in_world == 0) collecting = 0;

    if (collecting) {
        int best = -1;
        float bd = 1e9f;
        for (int i = 0; i < MAX_AMMO; i++) {
            const ball *b = &g->balls[i];
            if (!b->alive || b->age < PICKUP_MIN_AGE) continue;
            float dx = b->p.x - pl->p.x, dz = b->p.z - pl->p.z, d = dx * dx + dz * dz;
            if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0) {
            float want = atan2f(g->balls[best].p.x - pl->p.x, g->balls[best].p.z - pl->p.z);
            float dy = remainderf(want - pl->yaw, 2.0f * (float)M_PI);
            in->look_dx = fmaxf(-4 * dt, fminf(4 * dt, dy));
            in->look_dy = (-0.25f - pl->pitch) * fminf(1, dt * 5);
            in->move_f = 1.0f;
            in->sprint = 1;
        }
        return;
    }

    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const enemy *e = &g->en[i];
        if (!e->alive || e->dying > 0 || e->spawn_t < SPAWN_TIME) continue;
        float d = vector_magnitude(vector_sub(e->p, eye));
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return;
    const enemy *e = &g->en[best];
    float t = bd / SHOOT_SPEED;
    vector aim = vector_add(e->p, vector_scale(e->v, t));
    aim.y += 0.5f * GRAVITY * t * t;
    vector d = vector_sub(aim, eye);
    float want_yaw = atan2f(d.x, d.z);
    float want_pitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z));
    float dy = remainderf(want_yaw - pl->yaw, 2.0f * (float)M_PI);
    in->look_dx = fmaxf(-5 * dt, fminf(5 * dt, dy));
    in->look_dy = fmaxf(-3 * dt, fminf(3 * dt, want_pitch - pl->pitch));
    if (bd < 5.0f) in->move_f = -1.0f;   /* back-pedal */
    /* 0.6 s bursts with 0.4 s pauses, only while roughly on target. */
    float cycle = fmodf(burst_clock, 1.0f);
    in->fire = fabsf(dy) < 0.08f && cycle < 0.6f;
}

/* -------------------------------------------------------------- Render */

static scene_color lerp_color(scene_color a, scene_color b, float t) {
    return (scene_color){(uint8_t)(a.r + (b.r - a.r) * t),
                         (uint8_t)(a.g + (b.g - a.g) * t),
                         (uint8_t)(a.b + (b.b - a.b) * t)};
}

static int unlit_mat(scene *s, scene_color c) {
    scene_material m = scene_material_default();
    m.albedo = c;
    m.unlit = 1;
    return scene_add_material(s, m);
}

/* Box in the gun frame, optionally tilted about the gun's x axis. */
static void gun_box(scene *s, const gun_frame *gf, float cx, float cy, float cz,
                    float hx, float hy, float hz, float tilt, int m) {
    float c = cosf(tilt), sn = sinf(tilt);
    vector y = vector_add(vector_scale(gf->y, c), vector_scale(gf->z, sn));
    vector z = vector_sub(vector_scale(gf->z, c), vector_scale(gf->y, sn));
    scene_add_box(s, (scene_box){.center = gun_local(gf, cx, cy, cz),
                                 .half_extents = {hx, hy, hz},
                                 .ux = gf->x, .uy = y, .uz = z, .material = m});
}

static void gun_cyl(scene *s, const gun_frame *gf, float cx, float cy, float cz,
                    vector axis, float r, float hh, int m) {
    scene_add_cylinder(s, (scene_cylinder){.center = gun_local(gf, cx, cy, cz), .axis = axis,
                                           .radius = r, .half_height = hh, .material = m});
}

static void gun_ring(scene *s, const gun_frame *gf, float cx, float cy, float cz,
                     vector axis, float R, float r, int m) {
    scene_add_torus(s, (scene_torus){.center = gun_local(gf, cx, cy, cz), .axis = axis,
                                     .major_radius = R, .minor_radius = r, .material = m});
}

/* Futuristic sphere rifle: white receiver plates over dark gunmetal, a
 * vented barrel shroud whose cooling rings glow from cold blue to orange
 * as the barrel heats up, a side drum magazine with an ammo gauge, an
 * ammo strip along the receiver, holo sight, grip, stock and a muzzle
 * flash on every shot. */
static void build_viewmodel(scene *s, const game *g, const scene_camera *cam) {
    gun_frame gf = gun_frame_make(g, cam->origin, cam->forward, cam->right, cam->up);
    float load = (float)g->ammo / MAX_AMMO;
    float heat = g->pl.heat;

    scene_material plate = scene_material_default();
    plate.albedo = (scene_color){188, 194, 206};
    plate.reflectivity = 0.20f;
    int m_plate = scene_add_material(s, plate);
    scene_material metal = scene_material_default();
    metal.albedo = (scene_color){52, 56, 68};
    metal.reflectivity = 0.40f;
    int m_metal = scene_add_material(s, metal);
    scene_material dark = scene_material_default();
    dark.albedo = (scene_color){26, 28, 34};
    dark.reflectivity = 0.08f;
    int m_dark = scene_add_material(s, dark);

    scene_color ammo_c = load > 0.30f ? (scene_color){120, 245, 255}
                       : load > 0.12f ? (scene_color){255, 190, 60}
                                      : (scene_color){255, 70, 60};
    int m_ammo = unlit_mat(s, ammo_c);
    int m_ammo_dim = unlit_mat(s, lerp_color((scene_color){20, 26, 34}, ammo_c, 0.25f));
    scene_color heat_c = heat < 0.7f
        ? lerp_color((scene_color){40, 80, 120}, (scene_color){255, 140, 50}, heat / 0.7f)
        : lerp_color((scene_color){255, 140, 50}, (scene_color){255, 240, 200}, (heat - 0.7f) / 0.3f);
    int m_heat = unlit_mat(s, heat_c);

    vector X = gf.x, Z = gf.z;

    /* Receiver + top cover + rail. */
    gun_box(s, &gf, 0.0f,  0.000f,  0.02f, 0.042f, 0.046f, 0.150f, 0.0f, m_plate);
    gun_box(s, &gf, 0.0f, -0.040f,  0.02f, 0.044f, 0.012f, 0.140f, 0.0f, m_metal);
    gun_box(s, &gf, 0.0f,  0.054f,  0.00f, 0.024f, 0.009f, 0.125f, 0.0f, m_metal);
    for (int i = 0; i < 5; i++)
        gun_box(s, &gf, 0.0f, 0.066f, -0.08f + 0.04f * (float)i, 0.022f, 0.004f, 0.008f, 0.0f, m_dark);

    /* Ammo strip on both sides: dim track, bright fill proportional to load. */
    for (int side = -1; side <= 1; side += 2) {
        float xs = side * 0.0425f;
        gun_box(s, &gf, xs, 0.012f, 0.03f, 0.002f, 0.007f, 0.110f, 0.0f, m_ammo_dim);
        if (load > 0.0f) {
            float len = 0.110f * load;
            gun_box(s, &gf, side * 0.0440f, 0.012f, -0.08f + len, 0.002f, 0.0075f, len, 0.0f, m_ammo);
        }
    }

    /* Holo sight: post + floating reticle ring. */
    gun_box(s, &gf, 0.0f, 0.072f, -0.035f, 0.011f, 0.010f, 0.026f, 0.0f, m_metal);
    gun_ring(s, &gf, 0.0f, 0.100f, -0.035f, Z, 0.020f, 0.0040f, m_ammo);

    /* Shroud with glowing cooling rings, barrel, muzzle brake. */
    gun_cyl(s, &gf, 0.0f, GUN_MUZZLE_Y, 0.26f, Z, 0.027f, 0.090f, m_metal);
    for (int i = 0; i < 3; i++)
        gun_ring(s, &gf, 0.0f, GUN_MUZZLE_Y, 0.200f + 0.042f * (float)i, Z, 0.031f, 0.0085f, m_heat);
    gun_box(s, &gf, 0.0f, -0.030f, 0.24f, 0.022f, 0.014f, 0.070f, 0.0f, m_dark);  /* handguard */
    gun_cyl(s, &gf, 0.0f, GUN_MUZZLE_Y, 0.375f, Z, 0.012f, 0.035f, m_dark);
    gun_box(s, &gf, 0.0f, GUN_MUZZLE_Y, 0.402f, 0.019f, 0.011f, 0.014f, 0.0f, m_metal);
    gun_ring(s, &gf, 0.0f, GUN_MUZZLE_Y, GUN_MUZZLE_Z, Z, 0.015f, 0.0040f, m_ammo);

    /* Side drum magazine with a radial ammo gauge on each face. */
    gun_cyl(s, &gf, 0.0f, -0.078f, 0.06f, X, 0.052f, 0.030f, m_metal);
    for (int side = -1; side <= 1; side += 2) {
        vector n = vector_scale(X, (float)side);
        scene_add_disc(s, (scene_disc){.center = gun_local(&gf, side * 0.0305f, -0.078f, 0.06f),
                                       .normal = n, .radius = 0.040f, .material = m_ammo_dim});
        if (load > 0.0f)
            scene_add_disc(s, (scene_disc){.center = gun_local(&gf, side * 0.0312f, -0.078f, 0.06f),
                                           .normal = n, .radius = 0.040f * sqrtf(load),
                                           .material = m_ammo});
        gun_ring(s, &gf, side * 0.031f, -0.078f, 0.06f, X, 0.046f, 0.0045f, m_plate);
    }

    /* Pistol grip (raked back), trigger guard, stock + butt pad. */
    gun_box(s, &gf, 0.0f, -0.085f, -0.080f, 0.019f, 0.050f, 0.023f, 0.30f, m_dark);
    gun_ring(s, &gf, 0.0f, -0.058f, -0.030f, X, 0.019f, 0.0035f, m_metal);
    gun_box(s, &gf, 0.0f, -0.008f, -0.205f, 0.027f, 0.036f, 0.070f, 0.0f, m_plate);
    gun_box(s, &gf, 0.0f, -0.008f, -0.282f, 0.029f, 0.042f, 0.008f, 0.0f, m_dark);

    /* Muzzle flash for the first few frames after each shot. */
    if (g->pl.recoil > 0.55f) {
        float k = (g->pl.recoil - 0.55f) / 0.45f;
        int m_flash = unlit_mat(s, (scene_color){220, 255, 255});
        scene_add_sphere(s, (scene_sphere){.center = gun_local(&gf, 0.0f, GUN_MUZZLE_Y, GUN_MUZZLE_Z + 0.03f),
                                           .radius = 0.012f + 0.030f * k, .material = m_flash});
    }
}

static void build_scene(scene *s, const game *g, const scene_camera *cam) {
    scene_clear(s);
    scene_set_background(s, (scene_color){34, 40, 66}, -1);
    scene_set_ambient(s, 0.32f);
    scene_add_light(s, (scene_light){.direction = {0.45f, 0.85f, 0.30f}, .intensity = 0.80f});
    scene_add_light(s, (scene_light){.direction = {-0.5f, 0.35f, -0.6f}, .intensity = 0.25f});

    scene_material floor = scene_material_default();
    floor.albedo = (scene_color){70, 74, 92};
    floor.albedo2 = (scene_color){48, 51, 66};
    floor.tex_kind = SCENE_TEX_CHECKER;
    floor.tex_scale = 2.0f;
    floor.reflectivity = 0.22f;
    scene_add_plane(s, (scene_plane){.normal = {0, 1, 0}, .point = {0, 0, 0},
                                     .material = scene_add_material(s, floor)});

    scene_material wall = scene_material_default();
    wall.albedo = (scene_color){96, 100, 124};
    wall.albedo2 = (scene_color){70, 72, 92};
    wall.tex_kind = SCENE_TEX_NOISE;
    wall.tex_scale = 0.7f;
    int m_wall = scene_add_material(s, wall);
    int m_trim = unlit_mat(s, (scene_color){70, 200, 255});
    float H = ARENA_HALF, T = WALL_T;
    vector wmin[4] = {{-H - T, 0, -H - T}, {-H - T, 0, H}, {-H - T, 0, -H}, {H, 0, -H}};
    vector wmax[4] = {{H + T, WALL_H, -H}, {H + T, WALL_H, H + T}, {-H, WALL_H, H}, {H + T, WALL_H, H}};
    for (int i = 0; i < 4; i++) {
        scene_add_box(s, scene_box_aabb(wmin[i], wmax[i], m_wall));
        vector tmin = wmin[i], tmax = wmax[i];
        tmin.y = WALL_H; tmax.y = WALL_H + 0.08f;
        scene_add_box(s, scene_box_aabb(tmin, tmax, m_trim));
    }

    scene_material pil = scene_material_default();
    pil.albedo = (scene_color){200, 196, 186};
    pil.albedo2 = (scene_color){120, 116, 110};
    pil.tex_kind = SCENE_TEX_MARBLE;
    pil.tex_scale = 0.6f;
    pil.reflectivity = 0.30f;
    int m_pil = scene_add_material(s, pil);
    for (int i = 0; i < N_PILLARS; i++) {
        scene_add_cylinder(s, (scene_cylinder){
            .center = {PILLARS[i].x, PILLAR_H * 0.5f, PILLARS[i].z},
            .axis = {0, 1, 0}, .radius = PILLARS[i].r,
            .half_height = PILLAR_H * 0.5f, .material = m_pil});
        scene_add_torus(s, (scene_torus){
            .center = {PILLARS[i].x, 0.12f, PILLARS[i].z}, .axis = {0, 1, 0},
            .major_radius = PILLARS[i].r + 0.02f, .minor_radius = 0.12f, .material = m_trim});
    }

    /* Enemies. */
    int m_sclera = scene_add_material(s, scene_material_default());
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const enemy *e = &g->en[i];
        if (!e->alive || e->spawn_t < 0.0f) continue;
        const en_spec *sp = &SPECS[e->kind];
        float r = enemy_radius(e);
        float white = e->dying > 0.0f ? 0.4f + 0.6f * (e->dying / DIE_TIME) : e->flash;
        scene_material m = scene_material_default();
        m.albedo = lerp_color(sp->c1, (scene_color){255, 255, 255}, white);
        m.albedo2 = lerp_color(sp->c2, (scene_color){255, 255, 255}, white);
        m.tex_kind = SCENE_TEX_SPOTS;
        m.tex_scale = r * 0.55f;
        m.reflectivity = 0.18f;
        m.unlit = white > 0.6f;
        scene_add_sphere(s, (scene_sphere){.center = e->p, .radius = r,
                                           .material = scene_add_material(s, m)});
        if (e->dying > 0.0f) continue;
        vector f = vector_normalize((vector){e->face.x, 0.15f, e->face.z});
        scene_add_sphere(s, (scene_sphere){.center = vector_add(e->p, vector_scale(f, r * 0.80f)),
                                           .radius = r * 0.30f, .material = m_sclera});
        float glow = 0.6f + 0.4f * sinf(g->t * 6.0f + (float)i);
        int m_pupil = unlit_mat(s, (scene_color){255, (uint8_t)(40 + 60 * glow), 30});
        scene_add_sphere(s, (scene_sphere){.center = vector_add(e->p, vector_scale(f, r * 1.0f)),
                                           .radius = r * 0.14f, .material = m_pupil});
    }

    /* Balls: hot cyan in flight, pulsing at rest so they're easy to find. */
    int m_ball = unlit_mat(s, (scene_color){120, 245, 255});
    for (int i = 0; i < MAX_AMMO; i++) {
        const ball *b = &g->balls[i];
        if (!b->alive) continue;
        int m = m_ball;
        if (b->resting) {
            float pulse = 0.5f + 0.5f * sinf(g->t * 5.0f + (float)i);
            m = unlit_mat(s, lerp_color((scene_color){60, 160, 200}, (scene_color){200, 255, 255}, pulse));
        }
        scene_add_sphere(s, (scene_sphere){.center = b->p, .radius = BALL_R, .material = m});
    }

    for (int i = 0; i < MAX_PARTS && !bench_no_parts; i++) {
        const particle *p = &g->parts[i];
        if (!p->alive) continue;
        float k = p->life / p->max_life;
        scene_add_sphere(s, (scene_sphere){.center = p->p, .radius = p->r * (0.4f + 0.6f * k),
                                           .material = unlit_mat(s, p->c)});
    }

    if (!g->game_over && !bench_no_gun) build_viewmodel(s, g, cam);
    if (no_reflections)
        for (int i = 0; i < s->material_count; i++) s->materials[i].reflectivity = 0.0f;
}

static void draw_hud(const game *g, uint32_t *px, int w, int h) {
    hud_target t = {px, w, h};
    int sc = h / 150;
    if (sc < 1) sc = 1;
    char buf[64];

    if (g->pl.hurt_flash > 0) hud_tint(t, 0xFF2020u, g->pl.hurt_flash * 0.45f);

    /* Crosshair. */
    int cx = w / 2, cy = h / 2, gap = 2 * sc, len = 4 * sc;
    uint32_t xc = g->ammo > 0 ? 0xFFFFFFFFu : 0xFFFF5050u;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t c = pass ? xc : 0xFF000000u;
        int o = pass ? 0 : 1;
        hud_rect(t, cx - gap - len - o, cy - o, len + 2 * o, 1 + 2 * o, c);
        hud_rect(t, cx + gap + 1 - o, cy - o, len + 2 * o, 1 + 2 * o, c);
        hud_rect(t, cx - o, cy - gap - len - o, 1 + 2 * o, len + 2 * o, c);
        hud_rect(t, cx - o, cy + gap + 1 - o, 1 + 2 * o, len + 2 * o, c);
    }

    /* Ammo: count + magazine bar (bright = in hand, dim = out in the world). */
    {
        int aw = 60 * sc, ah = 4 * sc, ax = w - 6 * sc - aw, ay = h - 6 * sc - ah;
        snprintf(buf, sizeof buf, "%d/%d", g->ammo, MAX_AMMO);
        float ld = (float)g->ammo / MAX_AMMO;
        uint32_t ac = ld > 0.30f ? 0xFF78F5FFu : (ld > 0.12f ? 0xFFFFBE3Cu : 0xFFFF4A3Cu);
        hud_text(t, w - 6 * sc - hud_text_width(buf, sc * 2), ay - 17 * sc, buf, sc * 2, ac);
        hud_rect(t, ax - 1, ay - 1, aw + 2, ah + 2, 0xFF000000u);
        hud_rect(t, ax, ay, aw, ah, 0xFF203040u);
        hud_rect(t, ax, ay, aw * g->ammo / MAX_AMMO, ah, ac);
    }
    if (g->ammo == 0 && !g->game_over) {
        float blink = 0.5f + 0.5f * sinf(g->t * 8.0f);
        if (blink > 0.3f)
            hud_text_center(t, cy + 14 * sc, "OUT OF SPHERES - GO COLLECT THEM", sc, 0xFFFF7060u);
    }

    /* Health bar. */
    int bw = 60 * sc, bh = 4 * sc, bx = 6 * sc, by = h - 6 * sc - bh;
    hud_text(t, bx, by - 9 * sc, "HP", sc, 0xFFFFFFFFu);
    hud_rect(t, bx - 1, by - 1, bw + 2, bh + 2, 0xFF000000u);
    float hpk = g->pl.hp / PLAYER_HP;
    uint32_t hc = hpk > 0.5f ? 0xFF60E070u : (hpk > 0.25f ? 0xFFF0C040u : 0xFFF04040u);
    hud_rect(t, bx, by, (int)(bw * hpk), bh, hc);

    snprintf(buf, sizeof buf, "WAVE %d", g->wave);
    hud_text(t, 6 * sc, 6 * sc, buf, sc, 0xFFFFFFFFu);
    snprintf(buf, sizeof buf, "LEFT %d", enemies_left(g));
    hud_text(t, 6 * sc, 16 * sc, buf, sc, 0xFFB0B8D0u);
    snprintf(buf, sizeof buf, "SCORE %d", g->score);
    hud_text(t, w - 6 * sc - hud_text_width(buf, sc), 6 * sc, buf, sc, 0xFFFFE070u);
    snprintf(buf, sizeof buf, "KILLS %d", g->kills);
    hud_text(t, w - 6 * sc - hud_text_width(buf, sc), 16 * sc, buf, sc, 0xFFB0B8D0u);

    if (g->banner_t > 0.0f) {
        hud_text_center(t, h / 3, g->banner, sc * 3, 0xFFFFFFFFu);
        if (g->game_over) {
            snprintf(buf, sizeof buf, "SCORE %d - PRESS R TO RESTART", g->score);
            hud_text_center(t, h / 3 + 20 * sc, buf, sc, 0xFFFFE070u);
        }
    }
}

typedef struct {
    postfx_bloom_ctx *bloom;
    int bloom_on;
} fx_state;

static void render_frame(game *g, rt_renderer *rnd, scene *s, scene_camera *cam,
                         uint32_t *px, int w, int h, fx_state *fx) {
    vector eye = eye_pos(g);
    scene_camera_place(cam, eye, cam_forward(g->pl.yaw, g->pl.pitch));
    g->ear = eye;
    g->ear_right = cam->right;
    build_scene(s, g, cam);
    rt_viewport vp = {w, h, FOV};
    rt_renderer_render(rnd, s, cam, &vp, px, NULL);
    if (fx->bloom_on && fx->bloom) {
        postfx_bloom cfg = {1, 0.72f, 0.2f, 0.55f, 4, 2};
        postfx_bloom_apply(fx->bloom, px, w, h, &cfg);
    }
    postfx_vignette vig = {1, 0.35f, 0.55f};
    postfx_vignette_apply(px, w, h, &vig);
    draw_hud(g, px, w, h);
}

/* ------------------------------------------------------------- Headless */

static int write_ppm(const char *path, const uint32_t *px, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t rgb[3] = {(uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

static int run_shot(game *g, const char *path, float sim_seconds, int w, int h) {
    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!rnd) { fprintf(stderr, "CPU renderer unavailable\n"); return 1; }
    scene *s = scene_create();
    scene_camera *cam = scene_camera_create((vector){0, EYE_H, 0}, (vector){0, 0, 1});
    fx_state fx = {postfx_bloom_create(w, h), 1};

    const float dt = 1.0f / 60.0f;
    int frames = (int)(sim_seconds / dt);
    for (int f = 0; f < frames; f++) {
        scene_camera_place(cam, eye_pos(g), cam_forward(g->pl.yaw, g->pl.pitch));
        g->ear = eye_pos(g);
        g->ear_right = cam->right;
        input in;
        bot_input(g, &in, dt);
        game_update(g, &in, dt);
    }

    uint32_t *px = calloc((size_t)w * (size_t)h, sizeof *px);
    render_frame(g, rnd, s, cam, px, w, h, &fx);
    int rc = write_ppm(path, px, w, h);

    int in_world = 0, resting = 0;
    for (int i = 0; i < MAX_AMMO; i++) {
        in_world += g->balls[i].alive;
        resting += g->balls[i].alive && g->balls[i].resting;
    }
    fprintf(stderr, "sim %.1fs: wave %d, kills %d, score %d, hp %.0f, shots %d, hits %d, "
                    "ammo %d in hand / %d in world (%d resting)%s\n",
            sim_seconds, g->wave, g->kills, g->score, g->pl.hp, g->shots, g->hits,
            g->ammo, in_world, resting, g->game_over ? ", GAME OVER" : "");
    {
        float sum = 0; int n = 0;
        for (int i = 0; i < MAX_AMMO; i++) if (g->balls[i].alive) {
            sum += hypotf(g->balls[i].p.x - g->pl.p.x, g->balls[i].p.z - g->pl.p.z); n++; }
        if (n) fprintf(stderr, "balls in world: mean distance from player %.1f m\n", sum / n);
    }
    fprintf(stderr, "sound events:");
    for (int i = 0; i < SFX_COUNT; i++) fprintf(stderr, " %s=%d", sfx_name(i), sfx_count(i));
    fprintf(stderr, "\n-> %s\n", path);

    free(px);
    postfx_bloom_destroy(fx.bloom);
    scene_camera_destroy(cam);
    scene_destroy(s);
    rt_renderer_destroy(rnd);
    return rc == 0 ? 0 : 1;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Plays the bot for sim_seconds, then renders that frozen state `frames`
 * times per variant, each variant switching one feature off. */
static int run_bench(game *g, float sim_seconds, int w, int h, int frames) {
    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!rnd) { fprintf(stderr, "CPU renderer unavailable\n"); return 1; }
    scene *s = scene_create();
    scene_camera *cam = scene_camera_create((vector){0, EYE_H, 0}, (vector){0, 0, 1});
    const float dt = 1.0f / 60.0f;
    for (int f = 0; f < (int)(sim_seconds / dt); f++) {
        scene_camera_place(cam, eye_pos(g), cam_forward(g->pl.yaw, g->pl.pitch));
        g->ear = eye_pos(g);
        g->ear_right = cam->right;
        input in;
        bot_input(g, &in, dt);
        game_update(g, &in, dt);
    }
    uint32_t *px = calloc((size_t)w * (size_t)h, sizeof *px);
    fx_state fx = {postfx_bloom_create(w, h), 1};

    static const char *NAMES[] = {"full", "no reflections", "no rifle",
                                  "no particles", "no bloom", "bare (all off)"};
    double base = 0.0;
    for (int v = 0; v < 6; v++) {
        no_reflections = v == 1 || v == 5;
        bench_no_gun   = v == 2 || v == 5;
        bench_no_parts = v == 3 || v == 5;
        fx.bloom_on    = !(v == 4 || v == 5);
        render_frame(g, rnd, s, cam, px, w, h, &fx);   /* warm-up */
        double t0 = now_ms();
        for (int f = 0; f < frames; f++) render_frame(g, rnd, s, cam, px, w, h, &fx);
        double ms = (now_ms() - t0) / frames;
        if (v == 0) {
            base = ms;
            int balls = 0;
            for (int i = 0; i < MAX_AMMO; i++) balls += g->balls[i].alive;
            fprintf(stderr, "%dx%d, %d balls out, primitives: %d spheres, %d boxes, "
                            "%d cylinders, %d tori, %d discs\n",
                    w, h, balls, s->sphere_count, s->box_count, s->cylinder_count,
                    s->torus_count, s->disc_count);
        }
        fprintf(stderr, "  %-16s %7.2f ms/frame  (%5.1f FPS)  %+6.1f%%\n",
                NAMES[v], ms, 1000.0 / ms, v ? (ms - base) / base * 100.0 : 0.0);
    }
    free(px);
    postfx_bloom_destroy(fx.bloom);
    scene_camera_destroy(cam);
    scene_destroy(s);
    rt_renderer_destroy(rnd);
    return 0;
}

/* -------------------------------------------------------------- Display */

static void display_pixels(GLuint tex, GLuint fbo, const uint32_t *pixels,
                           int rw, int rh, int ww, int wh) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rw, rh, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, rw, rh, 0, wh, ww, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

static void usage(const char *argv0) {
    printf("Usage: %s [options]\n", argv0);
    printf("  --cpu              Start on the CPU tracer (default: OpenGL when available)\n");
    printf("  -G, --gpu          Start on the OpenGL backend (the default; kept for scripts)\n");
    printf("  --bot              Let the autopilot play\n");
    printf("  --fullscreen       Start in desktop fullscreen (F11 toggles)\n");
    printf("  --no-reflections   Matte surfaces only: much cheaper on the CPU tracer\n");
    printf("  --seed N           RNG seed\n");
    printf("  --shot out.ppm     Headless: bot plays --sim seconds, render one frame\n");
    printf("  --sim N            With --shot: seconds to simulate (default 6)\n");
    printf("  --res WxH          With --shot: output resolution (default 960x600)\n");
    printf("  --bench N          Headless: bot plays --sim seconds, then time N CPU\n"
           "                     renders per variant (features toggled off one by one)\n");
    printf("  --dump-sfx DIR     Write every synthesized sound as DIR/<name>.wav\n");
    printf("\nWASD move, mouse look, hold LMB/SPACE to fire, SHIFT sprint, walk over spheres to\n");
    printf("recollect them. M mouse capture, R restart, V cycle resolution (-/= step),\n");
    printf("TAB backend, B bloom,\n");
    printf("F11 fullscreen, ESC quit.\n");
}

int main(int argc, char *argv[]) {
    rt_backend preferred = RT_BACKEND_OPENGL;   /* falls back to CPU */
    const char *shot_path = NULL, *dump_dir = NULL;
    int shot_w = INIT_WINDOW_W, shot_h = INIT_WINDOW_H, bot = 0, start_fullscreen = 0;
    float sim_seconds = 6.0f;
    int bench_frames = 0;
    uint64_t seed = (uint64_t)time(NULL);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-G") || !strcmp(argv[i], "--gpu")) preferred = RT_BACKEND_OPENGL;
        else if (!strcmp(argv[i], "--cpu")) preferred = RT_BACKEND_CPU;
        else if (!strcmp(argv[i], "--bot")) bot = 1;
        else if (!strcmp(argv[i], "--fullscreen")) start_fullscreen = 1;
        else if (!strcmp(argv[i], "--no-reflections")) no_reflections = 1;
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot_path = argv[++i];
        else if (!strcmp(argv[i], "--sim") && i + 1 < argc) sim_seconds = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--dump-sfx") && i + 1 < argc) dump_dir = argv[++i];
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc) bench_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--res") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &shot_w, &shot_h) != 2 || shot_w <= 0 || shot_h <= 0) {
                fprintf(stderr, "Bad --res, expected WxH\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); usage(argv[0]); return 1; }
    }
    rng_state ^= seed * 0x2545F4914F6CDD1Dull;
    if (rng_state == 0) rng_state = 1;

    static game g;
    game_reset(&g);

    if (dump_dir) {
        sfx_init(0);
        int rc = sfx_dump_wav(dump_dir);
        sfx_shutdown();
        return rc == 0 ? 0 : 1;
    }
    if (bench_frames > 0) {
        sfx_init(0);
        int rc = run_bench(&g, sim_seconds, shot_w, shot_h, bench_frames);
        sfx_shutdown();
        return rc;
    }
    if (shot_path) {
        sfx_init(0);
        int rc = run_shot(&g, shot_path, sim_seconds, shot_w, shot_h);
        sfx_shutdown();
        return rc;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    sfx_init(1);

    int window_w = INIT_WINDOW_W, window_h = INIT_WINDOW_H, fullscreen = start_fullscreen;
    SDL_Window *window = NULL;
    SDL_GLContext gl_ctx = NULL;
    static const int GL_VERSIONS[][2] = {{4, 3}, {4, 1}, {3, 3}};
    for (int v = 0; v < 3 && !gl_ctx; v++) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_VERSIONS[v][0]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_VERSIONS[v][1]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        if (window) SDL_DestroyWindow(window);
        window = SDL_CreateWindow("Ricochet", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  window_w, window_h,
                                  SDL_WINDOW_OPENGL | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
        if (!window) break;
        gl_ctx = SDL_GL_CreateContext(window);
    }
    if (!window || !gl_ctx) {
        fprintf(stderr, "Window/GL context: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        sfx_shutdown();
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);
    /* Fullscreen-at-start: size the render buffer to the real screen. */
    SDL_GetWindowSize(window, &window_w, &window_h);

    rt_renderer *cpu_rnd = rt_renderer_available(RT_BACKEND_CPU) ? rt_renderer_create(RT_BACKEND_CPU) : NULL;
    rt_renderer *gpu_rnd = rt_renderer_available(RT_BACKEND_OPENGL) ? rt_renderer_create(RT_BACKEND_OPENGL) : NULL;
    if (!cpu_rnd && !gpu_rnd) {
        fprintf(stderr, "No renderers available\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        sfx_shutdown();
        SDL_Quit();
        return 1;
    }
    rt_renderer *active = (preferred == RT_BACKEND_OPENGL && gpu_rnd) ? gpu_rnd : (cpu_rnd ? cpu_rnd : gpu_rnd);
    fprintf(stderr, "Active: %s\n", rt_renderer_name(active));

    scene *s = scene_create();
    scene_camera *cam = scene_camera_create((vector){0, EYE_H, 0}, (vector){0, 0, 1});

    int res_idx = RES_DEFAULT_IDX;
    if (!res_usable(res_idx, window_h)) res_idx = res_step(res_idx, -1, 0, window_h);
    int render_w, render_h;
    res_size(res_idx, window_w, window_h, &render_w, &render_h);
    float res_toast = 0.0f;   /* seconds left to show the resolution note */
    uint32_t *pixels = calloc((size_t)render_w * (size_t)render_h, sizeof(uint32_t));
    fx_state fx = {postfx_bloom_create(render_w, render_h), 1};

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    int mouse_captured = 1, running = 1, fire_block = 0;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    Uint32 frame_last = SDL_GetTicks(), fps_last = frame_last;
    int fps_frames = 0;
    Uint32 render_ms = 0;
    char title[200];

    while (running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - frame_last) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        frame_last = now;
        int need_resize = 0;
        input in = {0};

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_MOUSEMOTION && mouse_captured) {
                in.look_dx += e.motion.xrel * MOUSE_SENS;
                in.look_dy -= e.motion.yrel * MOUSE_SENS;
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (!mouse_captured) {
                    /* This click only grabs the mouse; don't start firing
                     * until the button is released and pressed again. */
                    mouse_captured = 1;
                    fire_block = 1;
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                }
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                fire_block = 0;
            } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running = 0; break;
                case SDLK_r: game_reset(&g); break;
                case SDLK_b: fx.bloom_on = !fx.bloom_on; break;
                case SDLK_m:
                    mouse_captured = !mouse_captured;
                    SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
                    break;
                case SDLK_TAB:
                    if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                    else if (active == gpu_rnd && cpu_rnd) active = cpu_rnd;
                    fprintf(stderr, "Active: %s\n", rt_renderer_name(active));
                    break;
                case SDLK_F11:
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_GetWindowSize(window, &window_w, &window_h);
                    need_resize = 1;
                    break;
                case SDLK_v:
                    res_idx = res_step(res_idx, +1, 1, window_h);
                    need_resize = 1;
                    break;
                case SDLK_MINUS: case SDLK_KP_MINUS:
                    res_idx = res_step(res_idx, -1, 0, window_h);
                    need_resize = 1;
                    break;
                case SDLK_EQUALS: case SDLK_KP_PLUS:
                    res_idx = res_step(res_idx, +1, 0, window_h);
                    need_resize = 1;
                    break;
                default: break;
                }
            }
        }

        if (need_resize) {
            /* After a window change the current rung may be too tall. */
            if (!res_usable(res_idx, window_h)) res_idx = res_step(res_idx, -1, 0, window_h);
            res_size(res_idx, window_w, window_h, &render_w, &render_h);
            res_toast = 1.5f;
            free(pixels);
            pixels = calloc((size_t)render_w * (size_t)render_h, sizeof(uint32_t));
            glBindTexture(GL_TEXTURE_2D, display_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
            fprintf(stderr, "Render resolution: %dx%d%s\n", render_w, render_h,
                    RES_HEIGHTS[res_idx] == 0 ? " (native)" : "");
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        int trigger = keys[SDL_SCANCODE_SPACE] ||
                      (mouse_captured && !fire_block &&
                       (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK));
        if (bot) {
            bot_input(&g, &in, dt);
            in.fire |= trigger;
        } else {
            in.fire = trigger;
            in.move_f = (float)(keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]);
            in.move_s = (float)(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]);
            in.sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
            in.look_dx += (float)(keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT]) * LOOK_SPEED * dt;
            in.look_dy += (float)(keys[SDL_SCANCODE_UP] - keys[SDL_SCANCODE_DOWN]) * LOOK_SPEED * dt;
        }
        game_update(&g, &in, dt);

        Uint32 r0 = SDL_GetTicks();
        render_frame(&g, active, s, cam, pixels, render_w, render_h, &fx);
        render_ms += SDL_GetTicks() - r0;
        if (res_toast > 0.0f) {
            res_toast -= dt;
            char note[48];
            snprintf(note, sizeof note, "RENDER %dX%d%s", render_w, render_h,
                     RES_HEIGHTS[res_idx] == 0 ? " NATIVE" : "");
            int sc = render_h / 150;
            hud_text_center((hud_target){pixels, render_w, render_h},
                            render_h * 2 / 3, note, sc < 1 ? 1 : sc, 0xFFB0F0FFu);
        }
        display_pixels(display_tex, display_fbo, pixels, render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        if (now - fps_last >= 1000) {
            snprintf(title, sizeof title, "Ricochet  wave %d  ammo %d/%d  hp %.0f | %s %d FPS (%.1f ms, %dx%d)%s",
                     g.wave, g.ammo, MAX_AMMO, g.pl.hp, rt_renderer_name(active), fps_frames,
                     fps_frames ? (float)render_ms / fps_frames : 0.0f, render_w, render_h,
                     bot ? "  [BOT]" : "");
            SDL_SetWindowTitle(window, title);
            fps_frames = 0;
            render_ms = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    postfx_bloom_destroy(fx.bloom);
    free(pixels);
    scene_camera_destroy(cam);
    scene_destroy(s);
    sfx_shutdown();
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
