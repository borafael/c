/* Ricochet game state and rules: the player, enemies, waves, the sphere
 * ammo living in a physics world, scoring. No SDL, no rendering. */

#include "game.h"
#include "sfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ RNG */

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rng_u32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 32);
}
float frand(void) { return (rng_u32() >> 8) * (1.0f / 16777216.0f); }
float frange(float a, float b) { return a + (b - a) * frand(); }

void rng_seed(uint64_t seed) {
    rng_state ^= seed * 0x2545F4914F6CDD1Dull;
    if (rng_state == 0) rng_state = 1;
}

/* ---------------------------------------------------------------- World */

/* Pillars are physics columns: balls bounce off them, everything else is
 * pushed out of them. */
const physics_column PILLARS[N_PILLARS] = {
    {-7.0f, -7.0f, 0.9f, REST_WALL}, { 7.0f, -7.0f, 0.9f, REST_WALL},
    {-7.0f,  7.0f, 0.9f, REST_WALL}, { 7.0f,  7.0f, 0.9f, REST_WALL},
    { 0.0f, -11.0f, 0.6f, REST_WALL}, { 0.0f, 11.0f, 0.6f, REST_WALL},
    {-11.0f, 0.0f, 0.6f, REST_WALL}, { 11.0f, 0.0f, 0.6f, REST_WALL},
};

const en_spec SPECS[EN_KINDS] = {
    {0.55f, 4.2f,  5.0f, 10.0f,  3, 1000, {255, 160,  40}, {120,  50,  10}},
    {0.85f, 2.8f, 14.0f, 20.0f,  7, 2500, {230,  45,  80}, { 90,  10,  30}},
    {1.30f, 1.8f, 40.0f, 30.0f, 16, 6000, {150,  70, 230}, { 45,  15,  80}},
};
physics_body ball_get(const game *g, int k) {
    return physics_world_body(g->phys, BALL_ID(k));
}

vector cam_forward(float yaw, float pitch) {
    return (vector){cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)};
}

vector eye_pos(const game *g) {
    return (vector){g->pl.p.x, EYE_H + 0.04f * sinf(g->pl.bob), g->pl.p.z};
}


gun_frame gun_frame_make(const game *g, vector eye, vector f, vector r, vector u) {
    float kick = g->pl.recoil;
    float sx = 0.008f * sinf(g->pl.bob);
    float sy = 0.006f * fabsf(cosf(g->pl.bob));
    vector o = vector_add(eye, vector_scale(f, 0.56f - 0.035f * kick));
    o = vector_add(o, vector_scale(r, 0.19f + sx));
    o = vector_add(o, vector_scale(u, -0.19f + sy + 0.010f * kick));
    return (gun_frame){o, r, u, f};
}

vector gun_local(const gun_frame *gf, float x, float y, float z) {
    return vector_add(gf->o, vector_add(vector_scale(gf->x, x),
                      vector_add(vector_scale(gf->y, y), vector_scale(gf->z, z))));
}

/* --------------------------------------------------------------- Audio */

/* Scales every game sound: the title screen's background match plays
 * quietly. Menu clicks call sfx_play directly and ignore it. */
float game_volume = 1.0f;

static void game_sfx(sfx_id id, float gain, float pan, float rate) {
    sfx_play(id, gain * game_volume, pan, rate);
}

static void play_at(game *g, sfx_id id, vector at, float gain, float rate) {
    vector d = vector_sub(at, g->ear);
    float dist = vector_magnitude(d);
    float att = 1.0f / (1.0f + 0.10f * dist);
    float pan = dist > 0.01f ? vector_dot(vector_scale(d, 1.0f / dist), g->ear_right) * 0.8f : 0.0f;
    game_sfx(id, gain * att, pan, rate);
}

/* ------------------------------------------------------------- Helpers */

static void banner(game *g, const char *s) {
    snprintf(g->banner, sizeof g->banner, "%s", s);
    g->banner_t = 2.2f;
}

int enemies_left(const game *g) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g->en[i].alive && g->en[i].dying == 0.0f) n++;
    return n;
}

float enemy_radius(const enemy *e) {
    float r = SPECS[e->kind].r;
    if (e->dying > 0.0f) r *= 1.0f + 0.35f * (e->dying / DIE_TIME);
    return r;
}

/* Push a circle (xz) out of pillars. Returns the pillar normal hit, or
 * a zero vector if no contact. */
static vector push_out_pillars(const game *g, vector *p, float r) {
    return physics_world_push_out_columns(g->phys, p, r);
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
            vector hit = push_out_pillars(g, &q, r + 0.6f);
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

game_settings game_cfg = {DEFAULT_AMMO, -1, 1};

static void on_contact(void *user, physics_world *w, const physics_contact *c);

static physics_world *create_physics(game *g) {
    physics_config c = physics_default_config();
    c.max_bodies   = MAX_ENEMIES + AMMO_CAP;
    c.gravity      = 0.0f;              /* no mutual attraction */
    c.merge_on_contact = 0;
    c.num_threads  = 1;
    c.fall_gravity = GRAVITY;
    c.collisions   = 1;
    c.ground = 1;
    c.ground_y            = 0.0f;
    c.ground_restitution  = REST_FLOOR;
    c.ground_grip         = FLOOR_GRIP;
    c.ground_settle_speed = SETTLE_SPEED;
    c.roll_friction       = ROLL_FRICTION;
    c.sleep_speed         = SLEEP_SPEED;
    /* Walls are physically infinitely tall, so nothing escapes. */
    c.walls = 1;
    c.wall_min_x = c.wall_min_z = -ARENA_HALF;
    c.wall_max_x = c.wall_max_z =  ARENA_HALF;
    c.wall_restitution = REST_WALL;
    c.columns      = PILLARS;
    c.column_count = N_PILLARS;
    /* Substeps follow the fastest ball so nothing moves more than
     * MAX_SUBSTEP_DIST per step: fast shots can't tunnel through enemies
     * or each other, and a field of resting balls stays cheap. */
    c.max_substep_dist = MAX_SUBSTEP_DIST;
    c.min_substeps     = 4;
    c.max_substeps     = MAX_SUBSTEPS;
    c.on_contact   = on_contact;
    c.contact_user = g;
    return physics_world_create(&c);
}

/* Put ball k on the floor at a random spot (never on top of the player),
 * where it lands and falls asleep within its first substep. */
static void scatter_ball(game *g, int k) {
    vector p;
    do {
        p = (vector){frange(-ARENA_HALF + 1, ARENA_HALF - 1), BALL_R,
                     frange(-ARENA_HALF + 1, ARENA_HALF - 1)};
        push_out_pillars(g, &p, BALL_R);
    } while (hypotf(p.x - g->pl.p.x, p.z - g->pl.p.z) < 3.0f);
    physics_world_set_enabled(g->phys, BALL_ID(k), 1);
    physics_world_set_position(g->phys, BALL_ID(k), p);
}

void game_reset(game *g) {
    physics_world *w = g->phys;
    memset(g, 0, sizeof *g);
    if (w) physics_world_clear(w);
    else if (!(w = create_physics(g))) { fprintf(stderr, "physics: out of memory\n"); exit(1); }
    g->phys = w;
    /* Fixed IDs (see game): every enemy and ball body exists from the
     * start, disabled until it is in play. */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        physics_world_add(w, &(physics_body_desc){
            .type = PHYSICS_KINEMATIC, .mass = 1.0f, .restitution = REST_ENEMY});
        physics_world_set_enabled(w, i, 0);
    }
    for (int k = 0; k < AMMO_CAP; k++) {   /* magazine can grow mid-game */
        physics_world_add(w, &(physics_body_desc){
            .type = PHYSICS_DYNAMIC, .mass = BALL_MASS, .radius = BALL_R,
            .restitution = REST_BALL});
        physics_world_set_enabled(w, BALL_ID(k), 0);
    }
    g->pl.p = (vector){0, 0, -3.0f};
    g->pl.hp = PLAYER_HP;
    g->mag = game_cfg.mag;
    g->ammo = game_cfg.start_ammo < 0 ? game_cfg.mag : game_cfg.start_ammo;
    g->wave = game_cfg.start_wave - 1;   /* the first spawn_wave makes it game_cfg.start_wave */
    g->start_wave = game_cfg.start_wave;
    g->start_ammo = game_cfg.start_ammo;
    g->hs_rank = HS_UNRECORDED;
    g->wave_timer = 1.0f;
    /* Rounds you don't start with lie scattered on the floor. */
    for (int i = 0; i < g->mag - g->ammo; i++) scatter_ball(g, i);
}

/* Change the magazine mid-game without it becoming a free refill: new
 * balls are scattered on the floor (go collect them), and a smaller
 * magazine takes balls out of your hand first, then out of the world. */
void game_set_magazine(game *g, int mag) {
    physics_world *w = g->phys;
    for (int k = g->mag; k < mag; k++) scatter_ball(g, k);
    for (int k = g->mag - 1; k >= mag; k--) {
        physics_body b = ball_get(g, k);
        if (!b.enabled) { g->ammo--; continue; }   /* was in hand */
        /* Ball k is out in the world. Take one from the hand instead by
         * moving k's state into a held ball's slot below the new size. */
        int held = -1;
        for (int j = 0; j < mag && g->ammo > 0; j++)
            if (!ball_get(g, j).enabled) { held = j; break; }
        if (held >= 0) {
            physics_world_set_enabled(w, BALL_ID(held), 1);
            physics_world_set_position(w, BALL_ID(held), b.position);
            physics_world_set_velocity(w, BALL_ID(held), b.velocity);
            g->ammo--;
        }
        physics_world_set_enabled(w, BALL_ID(k), 0);
    }
    g->mag = mag;
}

/* Points for a kill: the enemy's value times the magazine factor at
 * that moment (a smaller magazine is harder and pays more). No wave
 * multiplier: later waves already pay more by having more enemies, and
 * a high starting wave skips the kills of the waves before it. */
/* Calibrated with the bot (60 games per size): the score multiplier that
 * evens out the expected score of each magazine size against the
 * default. A bigger magazine helps much more than a smaller one hurts,
 * hence the two exponents: 10 -> x1.62, 20 -> x1.27, 80 -> x0.52,
 * 200 -> x0.22. */
float mag_factor(int mag) {
    float r = (float)DEFAULT_AMMO / (float)mag;
    return powf(r, mag <= DEFAULT_AMMO ? 0.35f : 0.95f);
}

static int kill_points(const game *g, int kind) {
    /* Rounded to tens so scores stay readable. */
    return 10 * (int)lroundf((float)SPECS[kind].score * mag_factor(g->mag) / 10.0f);
}

static void enemy_damage(game *g, enemy *e, vector at) {
    e->flash = 1.0f;
    if (--e->hp > 0) return;
    e->dying = 1e-4f;
    g->kills++;
    g->score += kill_points(g, e->kind);
    play_at(g, SFX_POP, at, 1.0f, 1.25f - 0.25f * SPECS[e->kind].r);
    burst(g, e->p, SPECS[e->kind].c1, 14 + 6 * e->kind, 7.0f);
}

/* ------------------------------------------------------------- Physics */

/* Everything a collision means for the game: sounds, hits, knockback.
 * The physics world has already resolved it. Balls are the only dynamic
 * bodies, so c->body is always a ball. */
static void on_contact(void *user, physics_world *w, const physics_contact *c) {
    game *g = user;
    vector bp = physics_world_body_position(w, c->body);
    float imp = c->speed;
    switch (c->kind) {
    case PHYSICS_CONTACT_GROUND:
        if (imp > 1.0f)
            play_at(g, SFX_BOUNCE, bp, fminf(1.0f, 0.15f + imp / 14.0f),
                    frange(0.92f, 1.08f) + fminf(imp, 12.0f) * 0.01f);
        break;
    case PHYSICS_CONTACT_WALL:
        if (imp > 1.2f) play_at(g, SFX_WALL, bp, fminf(1.0f, imp / 18.0f), frange(0.9f, 1.1f));
        break;
    case PHYSICS_CONTACT_COLUMN:
        if (imp > 1.2f) play_at(g, SFX_WALL, bp, fminf(1.0f, imp / 18.0f), frange(0.75f, 0.85f));
        break;
    case PHYSICS_CONTACT_BOX:
        if (c->normal.y > 0.99f) {              /* landed on top: like the floor */
            if (imp > 1.0f)
                play_at(g, SFX_BOUNCE, bp, fminf(1.0f, 0.15f + imp / 14.0f),
                        frange(0.92f, 1.08f) + fminf(imp, 12.0f) * 0.01f);
        } else if (imp > 1.2f) {
            play_at(g, SFX_WALL, bp, fminf(1.0f, imp / 18.0f), frange(0.85f, 0.95f));
        }
        break;
    case PHYSICS_CONTACT_BODY:
        if (c->other >= MAX_ENEMIES) {          /* ball vs ball */
            if (imp > 1.0f) play_at(g, SFX_WALL, bp, fminf(0.8f, imp / 14.0f), 1.9f);
            break;
        }
        /* Ball vs enemy: shove the enemy (scaled for feel), maybe hurt it. */
        enemy *en = &g->en[c->other];
        float me = SPECS[en->kind].mass;
        vector kick = vector_scale(c->normal, -c->impulse / me * KNOCKBACK);
        vector ev = physics_world_body_velocity(w, c->other);
        ev.x += kick.x;
        ev.z += kick.z;
        physics_world_set_velocity(w, c->other, ev);
        int can_hurt = en->dying == 0.0f && en->spawn_t >= SPAWN_TIME;
        if (imp > HIT_MIN_SPEED && can_hurt) {
            g->hits++;
            play_at(g, SFX_HIT, bp, fminf(1.0f, 0.35f + imp / 25.0f),
                    1.3f - 0.35f * SPECS[en->kind].r + frange(-0.04f, 0.04f));
            burst(g, bp, (scene_color){200, 250, 255}, 4, 3.0f);
            enemy_damage(g, en, bp);
            physics_world_set_radius(w, c->other, enemy_radius(en));
        } else if (imp > 1.2f) {
            play_at(g, SFX_WALL, bp, fminf(0.7f, imp / 12.0f), 1.3f);
        }
        break;
    }
}

/* Balls fly and bounce; enemies take part as kinematic bodies (their own
 * steering moves them in update_enemies), and get knocked back above. */
static void step_physics(game *g, float dt) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const enemy *e = &g->en[i];
        int on = e->alive && e->spawn_t >= 0.0f;
        physics_world_set_enabled(g->phys, i, on);
        if (!on) continue;
        physics_world_set_position(g->phys, i, e->p);
        physics_world_set_velocity(g->phys, i, e->v);
        physics_world_set_radius(g->phys, i, enemy_radius(e));
        physics_world_set_mass(g->phys, i, SPECS[e->kind].mass);
    }
    physics_world_advance(g->phys, dt);
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g->en[i].alive && g->en[i].spawn_t >= 0.0f)
            g->en[i].v = physics_world_body_velocity(g->phys, i);
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
        vector n = push_out_pillars(g, &e->p, s->r);
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
            game_sfx(SFX_HURT, 0.9f, 0.0f, 1.0f);
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
        game_sfx(SFX_DRY, 0.7f, 0.0f, frange(0.95f, 1.05f));
        return;
    }
    int slot = -1;
    for (int i = 0; i < g->mag; i++) if (!ball_get(g, i).enabled) { slot = i; break; }
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
    vector hit = push_out_pillars(g, &probe, BALL_R);
    if (hit.x != 0 || hit.z != 0) muzzle = vector_add(eye, vector_scale(fwd, 0.3f));
    vector target = vector_add(eye, vector_scale(fwd, 25.0f));
    vector dir = vector_normalize(vector_sub(target, muzzle));
    dir = vector_normalize(vector_add(dir, vector_add(vector_scale(right, frange(-SPREAD, SPREAD)),
                                                      vector_scale(up, frange(-SPREAD, SPREAD)))));
    vector v = vector_add(vector_scale(dir, SHOOT_SPEED), vector_scale(pl->v, 0.5f));

    physics_world_set_enabled(g->phys, BALL_ID(slot), 1);
    physics_world_set_position(g->phys, BALL_ID(slot), muzzle);
    physics_world_set_velocity(g->phys, BALL_ID(slot), v);
    g->ammo--;
    g->shots++;
    pl->recoil = 1.0f;
    pl->heat = fminf(1.0f, pl->heat + 0.045f);
    /* Rapid fire: quieter, shorter-feeling and pitch-jittered so a burst
     * reads as a rattle rather than one droning tone. */
    game_sfx(SFX_SHOOT, 0.55f, 0.0f, frange(1.05f, 1.25f));
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
    push_out_pillars(g, &pl->p, PLAYER_R);
    float hs = sqrtf(pl->v.x * pl->v.x + pl->v.z * pl->v.z);
    pl->bob += hs * dt * 1.6f;

    if (in->fire) fire(g);

    /* Recollect: walk over (or catch) a ball. */
    for (int i = 0; i < g->mag; i++) {
        physics_body b = ball_get(g, i);
        if (!b.enabled || b.age < PICKUP_MIN_AGE || b.position.y > 2.2f) continue;
        float dx = b.position.x - pl->p.x, dz = b.position.z - pl->p.z;
        if (dx * dx + dz * dz > PICKUP_R * PICKUP_R) continue;
        physics_world_set_enabled(g->phys, BALL_ID(i), 0);
        g->ammo++;
        game_sfx(SFX_PICKUP, 0.45f, 0.0f, 0.9f + 0.6f * (float)g->ammo / g->mag);
    }
}

void game_update(game *g, const input *in, float dt) {
    g->t += dt;
    g->banner_t -= dt;
    update_player(g, in, dt);
    step_physics(g, dt);
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
