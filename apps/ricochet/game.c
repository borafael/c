/* Ricochet game state and rules: the player, the sphere ammo living in a
 * physics world, the grenade, scoring, the frame. Enemies are in enemy.c
 * (see game_internal.h). No SDL, no rendering. */

#include "game_internal.h"

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

void game_sfx(sfx_id id, float gain, float pan, float rate) {
    sfx_play(id, gain * game_volume, pan, rate);
}

void play_at(game *g, sfx_id id, vector at, float gain, float rate) {
    vector d = vector_sub(at, g->ear);
    float dist = vector_magnitude(d);
    float att = 1.0f / (1.0f + 0.10f * dist);
    float pan = dist > 0.01f ? vector_dot(vector_scale(d, 1.0f / dist), g->ear_right) * 0.8f : 0.0f;
    game_sfx(id, gain * att, pan, rate);
}

/* ------------------------------------------------------------- Helpers */

void banner(game *g, const char *s) {
    snprintf(g->banner, sizeof g->banner, "%s", s);
    g->banner_t = 2.2f;
}

vector push_out_static(const game *g, vector *p, float r) {
    vector n = physics_world_push_out_columns(g->phys, p, r);
    if (g->lv->n_boxes > 0) {
        vector nb = physics_world_push_out_boxes(g->phys, p, r);
        if (nb.x != 0 || nb.z != 0) n = nb;
    }
    return n;
}

void clamp_arena(const game *g, vector *p, float r) {
    float lx = g->lv->half_x - r, lz = g->lv->half_z - r;
    if (p->x >  lx) p->x =  lx;
    if (p->x < -lx) p->x = -lx;
    if (p->z >  lz) p->z =  lz;
    if (p->z < -lz) p->z = -lz;
}

void burst(game *g, vector at, scene_color c, int n, float speed) {
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

/* ------------------------------------------------------ Level and reset */

static level builtin_arena;
static const level *arena(void) {
    static int ready;
    if (!ready) { level_builtin_arena(&builtin_arena); ready = 1; }
    return &builtin_arena;
}

game_settings game_cfg = {DEFAULT_AMMO, -1, 1, NULL, 0, GRENADE_DEFAULT_PAYLOAD};

static void on_contact(void *user, physics_world *w, const physics_contact *c);

static physics_world *create_physics(game *g) {
    physics_config c = physics_default_config();
    c.max_bodies   = MAX_ENEMIES + AMMO_CAP + 1;   /* + the grenade */
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
    /* Outer walls are physically infinitely tall, so nothing escapes. */
    const level *lv = g->lv;
    c.walls = 1;
    c.wall_min_x = -lv->half_x;
    c.wall_max_x =  lv->half_x;
    c.wall_min_z = -lv->half_z;
    c.wall_max_z =  lv->half_z;
    c.wall_restitution = REST_WALL;
    /* The physics world copies these at create. */
    static physics_column cols[LEVEL_MAX_PILLARS];
    static physics_box boxes[LEVEL_MAX_BOXES];
    for (int i = 0; i < lv->n_pillars; i++)
        cols[i] = (physics_column){lv->pillars[i].x, lv->pillars[i].z, lv->pillars[i].r, REST_WALL};
    for (int i = 0; i < lv->n_boxes; i++)
        boxes[i] = (physics_box){lv->boxes[i].min, lv->boxes[i].max, REST_WALL};
    c.columns      = cols;
    c.column_count = lv->n_pillars;
    c.boxes        = boxes;
    c.box_count    = lv->n_boxes;
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
        p = (vector){frange(-g->lv->half_x + 1, g->lv->half_x - 1), BALL_R,
                     frange(-g->lv->half_z + 1, g->lv->half_z - 1)};
        push_out_static(g, &p, BALL_R);
    } while (hypotf(p.x - g->pl.p.x, p.z - g->pl.p.z) < 3.0f);
    physics_world_set_enabled(g->phys, BALL_ID(k), 1);
    physics_world_set_position(g->phys, BALL_ID(k), p);
}

/* Build (or rebuild, after a level switch / reload) everything that
 * comes from the level: the physics world and the nav grid. */
static void build_level(game *g) {
    if (g->phys) physics_world_destroy(g->phys);
    for (int k = 0; k <= EN_KINDS; k++) {
        if (k < EN_KINDS) nav_field_free(&g->to_player[k]);
        nav_free(&g->nav[k]);
    }
    nav_field_free(&g->to_ball);
    g->phys = create_physics(g);
    int ok = g->phys != NULL;
    for (int k = 0; k <= EN_KINDS && ok; k++) {
        float r = k == NAV_PLAYER ? PLAYER_R
                : SPECS[k].r + SPECS[k].grow * (float)SPECS[k].cap;   /* full up */
        float clearance = r + 0.05f;
        ok = nav_build(&g->nav[k], g->lv, clearance) == 0 &&
             (k == NAV_PLAYER || nav_field_init(&g->to_player[k], &g->nav[k]) == 0);
    }
    ok = ok && nav_field_init(&g->to_ball, &g->nav[EN_EATER]) == 0;
    if (!ok) {
        fprintf(stderr, "out of memory building the level\n");
        exit(1);
    }
    g->built_rev = game_cfg.level_rev;
}

void game_reset(game *g) {
    if (!game_cfg.level) game_cfg.level = arena();
    /* Keep what was built from the level across the wipe. */
    physics_world *w = g->phys;
    const level *built = g->lv;
    int rev = g->built_rev;
    nav_grid nav[EN_KINDS + 1];
    nav_field to_player[EN_KINDS], to_ball = g->to_ball;
    memcpy(nav, g->nav, sizeof nav);
    memcpy(to_player, g->to_player, sizeof to_player);
    memset(g, 0, sizeof *g);
    g->phys = w;
    g->lv = built;
    g->built_rev = rev;
    memcpy(g->nav, nav, sizeof nav);
    memcpy(g->to_player, to_player, sizeof to_player);
    g->to_ball = to_ball;
    if (!w || built != game_cfg.level || rev != game_cfg.level_rev) {
        g->lv = game_cfg.level;
        build_level(g);
    } else {
        physics_world_clear(w);
        for (int k = 0; k < EN_KINDS; k++) g->to_player[k].target = -1;
        g->to_ball.target = -1;
    }
    w = g->phys;
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
    physics_world_add(w, &(physics_body_desc){
        .type = PHYSICS_DYNAMIC, .mass = 2.0f, .radius = GRENADE_R, .restitution = 0.3f});
    physics_world_set_enabled(w, GRENADE_ID, 0);
    g->pl.p = g->lv->player;
    g->pl.yaw = g->lv->player_yaw;
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
    /* A grenade in the air would be neither in hand nor in the world:
     * call it back, payload into the hand. */
    if (g->gren.state == GREN_FLYING) {
        physics_world_set_enabled(w, GRENADE_ID, 0);
        g->ammo += g->gren.payload;
        g->gren = (grenade){.held = g->gren.held};
    }
    /* Same for balls inside eaters: out they come. */
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g->en[i].alive && g->en[i].eaten > 0) enemy_release(g, &g->en[i], g->en[i].eaten, 0);
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

/* ------------------------------------------------------------- Physics */

/* Everything a collision means for the game: sounds, hits, knockback.
 * The physics world has already resolved it. Balls are the only dynamic
 * bodies, so c->body is always a ball. */
static void on_contact(void *user, physics_world *w, const physics_contact *c) {
    game *g = user;
    /* The grenade's first touch of anything sets it off; the burst itself
     * happens after the physics step (see grenade_burst). */
    if (c->body == GRENADE_ID || c->other == GRENADE_ID) {
        grenade *gr = &g->gren;
        if (gr->state == GREN_FLYING && !gr->burst) {
            gr->burst = 1;
            gr->burst_kind = c->kind;
            gr->burst_p = physics_world_body_position(w, GRENADE_ID);
            gr->burst_n = c->body == GRENADE_ID ? c->normal : vector_scale(c->normal, -1.0f);
        }
        return;
    }
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
/* Box tops are slick in this game: a ball resting on one slides to the
 * nearest edge and drops off, so no ammo is ever stranded out of reach
 * on top of a big crate. */
#define BOX_SLIDE_SPEED 2.0f
static void slide_off_box_tops(game *g, float dt) {
    const level *lv = g->lv;
    for (int k = 0; k < g->mag; k++) {
        physics_body b = ball_get(g, k);
        vector p = b.position;
        if (!b.enabled || p.y < BALL_R + 0.05f || vector_magnitude(b.velocity) > 0.5f) continue;
        for (int i = 0; i < lv->n_boxes; i++) {
            const level_box *bx = &lv->boxes[i];
            if (p.x < bx->min.x || p.x > bx->max.x || p.z < bx->min.z || p.z > bx->max.z ||
                fabsf(p.y - (bx->max.y + BALL_R)) > 0.05f)
                continue;
            /* Nearest edge. */
            float d[4] = {p.x - bx->min.x, bx->max.x - p.x, p.z - bx->min.z, bx->max.z - p.z};
            int e = 0;
            for (int j = 1; j < 4; j++) if (d[j] < d[e]) e = j;
            float step = BOX_SLIDE_SPEED * dt;
            if (step > d[e]) step = d[e] + BALL_R + 0.01f;   /* over the edge: falls */
            if (e == 0) p.x -= step;
            if (e == 1) p.x += step;
            if (e == 2) p.z -= step;
            if (e == 3) p.z += step;
            physics_world_set_position(g->phys, BALL_ID(k), p);
            break;
        }
    }
}

/* Spray the payload out of the grenade as ordinary balls: a dome off the
 * surface it hit (floor, wall, crate, pillar), every way off a body (an
 * enemy, a ball, or bursting in mid-air). */
static void grenade_burst(game *g) {
    grenade *gr = &g->gren;
    physics_world *w = g->phys;
    physics_world_set_enabled(w, GRENADE_ID, 0);
    vector p = gr->burst_p, n = gr->burst_n;
    int dome = gr->burst_kind != PHYSICS_CONTACT_BODY;
    n = vector_magnitude(n) > 1e-4f ? vector_normalize(n) : (vector){0, 1, 0};
    if (p.y < GRENADE_R) p.y = GRENADE_R;
    float spread = 0.22f + 0.012f * (float)gr->payload;   /* room for them all */
    int left = gr->payload;
    for (int k = 0; k < g->mag && left > 0; k++) {
        if (ball_get(g, k).enabled) continue;   /* in hand or payload: same thing */
        vector d;
        do d = (vector){frange(-1, 1), frange(-1, 1), frange(-1, 1)};
        while (vector_magnitude(d) > 1.0f || vector_magnitude(d) < 0.1f);
        d = vector_normalize(d);
        float dn = vector_dot(d, n);
        if (dome && dn < 0) d = vector_sub(d, vector_scale(n, 2.0f * dn));
        if (dome) d = vector_normalize(vector_add(d, vector_scale(n, 0.3f)));
        vector bp = vector_add(p, vector_scale(d, spread));
        if (bp.y < BALL_R + 0.01f) bp.y = BALL_R + 0.01f;
        clamp_arena(g, &bp, BALL_R);
        push_out_static(g, &bp, BALL_R);
        physics_world_set_enabled(w, BALL_ID(k), 1);
        physics_world_set_position(w, BALL_ID(k), bp);
        physics_world_set_velocity(w, BALL_ID(k), vector_scale(d, GRENADE_BURST_V * frange(0.6f, 1.0f)));
        g->shots++;
        left--;
    }
    burst(g, p, (scene_color){255, 170, 60}, 22, 10.0f);
    burst(g, p, (scene_color){255, 255, 220}, 10, 6.0f);
    play_at(g, SFX_POP, p, 1.0f, 0.45f);
    play_at(g, SFX_WALL, p, 1.0f, 0.6f);
    gr->state = GREN_IDLE;
    gr->payload = 0;
    gr->burst = 0;
}

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
    if (g->gren.burst) grenade_burst(g);
    if (g->lv->n_boxes > 0) slide_off_box_tops(g, dt);
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g->en[i].alive && g->en[i].spawn_t >= 0.0f)
            g->en[i].v = physics_world_body_velocity(g->phys, i);
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
     * If the muzzle is poking into a pillar or wall, spawn just in front of the
     * eye instead so the ball can't tunnel out the far side. */
    gun_frame gf = gun_frame_make(g, eye, fwd, right, up);
    vector muzzle = gun_local(&gf, 0.0f, GUN_MUZZLE_Y, GUN_MUZZLE_Z + 0.03f);
    vector probe = muzzle;
    vector hit = push_out_static(g, &probe, BALL_R);
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

/* ------------------------------------------------------------ Grenade */

/* Where the grenade leaves the hand and how fast, for a throw at power p:
 * along the aim, lofted into an arc. */
static void grenade_launch(const game *g, float power, vector *pos, vector *vel) {
    const player *pl = &g->pl;
    vector eye = eye_pos(g);
    vector fwd = cam_forward(pl->yaw, pl->pitch);
    vector dir = vector_normalize(vector_add(fwd, (vector){0, GRENADE_LOFT, 0}));
    vector p = vector_add(eye, vector_scale(fwd, 0.45f));
    p.y -= 0.15f;
    vector probe = p;
    vector hit = push_out_static(g, &probe, GRENADE_R);
    if (hit.x != 0 || hit.z != 0) p = eye;   /* hand in a wall: from the eye */
    float speed = GRENADE_V_MIN + (GRENADE_V_MAX - GRENADE_V_MIN) * fminf(fmaxf(power, 0.0f), 1.0f);
    *pos = p;
    *vel = vector_add(vector_scale(dir, speed), (vector){pl->v.x * 0.5f, 0, pl->v.z * 0.5f});
}

int grenade_arc(const game *g, float power, vector *pts, int max) {
    const level *lv = g->lv;
    const float dt = 0.04f, r = GRENADE_R;
    vector p, v;
    grenade_launch(g, power, &p, &v);
    int n = 0;
    while (n < max) {
        pts[n++] = p;
        vector q = vector_add(p, vector_scale(v, dt));
        v.y -= GRAVITY * dt;
        int stop = 0;
        if (q.y <= r) {                                   /* floor */
            float k = (p.y - r) / fmaxf(p.y - q.y, 1e-6f);
            q = vector_add(p, vector_scale(vector_sub(q, p), k));
            stop = 1;
        }
        if (fabsf(q.x) > lv->half_x - r || fabsf(q.z) > lv->half_z - r) {
            clamp_arena(g, &q, r);
            stop = 1;
        }
        for (int i = 0; i < lv->n_pillars && !stop; i++) {
            float dx = q.x - lv->pillars[i].x, dz = q.z - lv->pillars[i].z, rr = lv->pillars[i].r + r;
            stop = dx * dx + dz * dz < rr * rr;
        }
        for (int i = 0; i < lv->n_boxes && !stop; i++) {
            const level_box *b = &lv->boxes[i];
            stop = q.x > b->min.x - r && q.x < b->max.x + r && q.z > b->min.z - r &&
                   q.z < b->max.z + r && q.y < b->max.y + r;
        }
        for (int i = 0; i < MAX_ENEMIES && !stop; i++) {
            const enemy *e = &g->en[i];
            if (!e->alive || e->dying > 0 || e->spawn_t < 0) continue;
            float rr = enemy_radius(e) + r;
            stop = vector_magnitude(vector_sub(q, e->p)) < rr;
        }
        if (stop) {
            if (n < max) pts[n++] = q;
            break;
        }
        p = q;
    }
    return n;
}

/* Payloads the wheel / Q, E step through, up to the magazine size. */
static const int PAYLOADS[] = {3, 5, 10, 15, 20, 25, 30, 40};
#define N_PAYLOADS ((int)(sizeof PAYLOADS / sizeof PAYLOADS[0]))

static void step_payload(game *g, int dir) {
    int ladder[N_PAYLOADS + 1], n = 0;
    for (int i = 0; i < N_PAYLOADS && PAYLOADS[i] < g->mag; i++) ladder[n++] = PAYLOADS[i];
    if (g->mag <= PAYLOADS[N_PAYLOADS - 1]) ladder[n++] = g->mag;   /* "all of them" */
    int cur = game_cfg.grenade_payload, at = 0;
    for (int i = 0; i < n; i++) if (ladder[i] <= cur) at = i;
    at += dir;
    if (at < 0) at = 0;
    if (at >= n) at = n - 1;
    if (ladder[at] != cur) game_sfx(SFX_PICKUP, 0.35f, 0.0f, 0.8f + 0.04f * (float)at);
    game_cfg.grenade_payload = ladder[at];
}

static void update_grenade(game *g, const input *in, float dt) {
    grenade *gr = &g->gren;
    if (in->payload_step) step_payload(g, in->payload_step > 0 ? 1 : -1);
    int pressed = in->grenade && !gr->held, released = !in->grenade && gr->held;
    gr->held = in->grenade;
    switch (gr->state) {
    case GREN_IDLE:
        if (!pressed || g->game_over) break;
        if (g->ammo <= 0) { game_sfx(SFX_DRY, 0.7f, 0.0f, 0.8f); break; }
        gr->state = GREN_CHARGING;
        gr->power = 0.0f;
        gr->tick = 0.1f;
        break;
    case GREN_CHARGING: {
        gr->power = fminf(1.0f, gr->power + dt / GRENADE_CHARGE);
        if (gr->power >= gr->tick - 1e-4f && gr->tick <= 1.0f) {
            game_sfx(SFX_PICKUP, 0.30f, 0.0f, 0.7f + 0.9f * gr->tick);
            gr->tick += 0.1f;
        }
        if (g->game_over) { gr->state = GREN_IDLE; break; }
        if (!released) break;
        int n = game_cfg.grenade_payload < g->ammo ? game_cfg.grenade_payload : g->ammo;
        if (n <= 0) { gr->state = GREN_IDLE; game_sfx(SFX_DRY, 0.7f, 0.0f, 0.8f); break; }
        vector p, v;
        grenade_launch(g, gr->power, &p, &v);
        physics_world_set_enabled(g->phys, GRENADE_ID, 1);
        physics_world_set_position(g->phys, GRENADE_ID, p);
        physics_world_set_velocity(g->phys, GRENADE_ID, v);
        g->ammo -= n;
        gr->payload = n;
        gr->state = GREN_FLYING;
        gr->age = 0.0f;
        gr->burst = 0;
        g->pl.recoil = 0.6f;
        game_sfx(SFX_SHOOT, 0.8f, 0.0f, 0.55f);
        break;
    }
    case GREN_FLYING:
        gr->age += dt;
        if (gr->age > GRENADE_LIFE && !gr->burst) {      /* never touched anything */
            gr->burst = 1;
            gr->burst_kind = PHYSICS_CONTACT_BODY;
            gr->burst_p = physics_world_body_position(g->phys, GRENADE_ID);
            gr->burst_n = (vector){0, 1, 0};
        }
        break;
    }
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
    clamp_arena(g, &pl->p, PLAYER_R);
    push_out_static(g, &pl->p, PLAYER_R);
    float hs = sqrtf(pl->v.x * pl->v.x + pl->v.z * pl->v.z);
    pl->bob += hs * dt * 1.6f;

    if (in->fire) fire(g);
    update_grenade(g, in, dt);

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
