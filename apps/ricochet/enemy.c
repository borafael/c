/* Enemies: the kind table, waves and spawning, steering around walls, and
 * what the special kinds do differently. Part of the game rules; see
 * game_internal.h for how it splits with game.c.
 *
 * Adding a kind: an en_kind, a SPECS row (numbers, look, first wave) and,
 * if it behaves differently, hook functions in a section of its own here.
 * A new look also needs its decor drawn in render.c. */

#include "game_internal.h"

#include <math.h>
#include <stdio.h>

/* ------------------------------------------------------------- Queries */

int enemies_left(const game *g) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g->en[i].alive && g->en[i].dying == 0.0f) n++;
    return n;
}

float enemy_size(const enemy *e) {
    const en_spec *s = &SPECS[e->kind];
    return s->r + s->grow * (float)e->eaten;
}

float enemy_radius(const enemy *e) {
    float r = enemy_size(e);
    if (e->dying > 0.0f) r *= 1.0f + 0.35f * (e->dying / DIE_TIME);
    return r;
}

/* ------------------------------------------------------------ Spawning */

/* Somewhere for an enemy of radius r to rise from: near a spawn point if
 * the level has them, else anywhere at least 11 m from the player; never
 * in a pillar or wall. */
static vector spawn_point(game *g, float r) {
    const level *lv = g->lv;
    vector p = {0, r, 0};
    for (int tries = 0; tries < 100; tries++) {
        if (lv->n_spawns > 0) {
            /* Near one of the level's spawn points. */
            vector sp = lv->spawns[(int)(frand() * (float)lv->n_spawns) % lv->n_spawns];
            p.x = sp.x + frange(-1.0f, 1.0f);
            p.z = sp.z + frange(-1.0f, 1.0f);
            clamp_arena(g, &p, r + 0.5f);
            float dx = p.x - g->pl.p.x, dz = p.z - g->pl.p.z;
            if (tries < 50 && dx * dx + dz * dz < 8.0f * 8.0f) continue;
        } else {
            p.x = frange(-lv->half_x + r + 1, lv->half_x - r - 1);
            p.z = frange(-lv->half_z + r + 1, lv->half_z - r - 1);
            float dx = p.x - g->pl.p.x, dz = p.z - g->pl.p.z;
            if (dx * dx + dz * dz < 11.0f * 11.0f) continue;
        }
        vector q = p;
        vector hit = push_out_static(g, &q, r + 0.6f);
        if (hit.x != 0 || hit.z != 0) continue;
        break;
    }
    return p;
}

void spawn_wave(game *g) {
    g->wave++;
    int count = 2 + g->wave;
    if (count > MAX_ENEMIES) count = MAX_ENEMIES;
    float big_p = fminf(0.08f * (float)(g->wave - 1), 0.35f);
    /* Specials that have arrived by now (kind order), the earliest
     * special's wave, and the one this wave introduces. */
    int specials[EN_KINDS], ns = 0, first = 0, intro = -1;
    for (int k = 0; k < EN_KINDS; k++) {
        int fw = SPECS[k].first_wave;
        if (fw <= 0) continue;
        if (first == 0 || fw < first) first = fw;
        if (g->wave >= fw) specials[ns++] = k;
        if (g->wave == fw) intro = k;
    }
    int placed = 0;
    for (int i = 0; i < MAX_ENEMIES && placed < count; i++) {
        if (g->en[i].alive) continue;
        float roll = frand();
        int kind = roll < big_p ? EN_BIG : (roll < big_p + 0.45f ? EN_SMALL : EN_MED);
        /* Specials take a growing share once they have arrived; each one's
         * first wave leads with one of it. (Waves before the first special
         * draw exactly as they always did.) */
        if (ns > 0) {
            float share = fminf(0.15f + 0.03f * (float)(g->wave - first), 0.45f);
            if (frand() < share) kind = specials[(int)(frand() * (float)ns) % ns];
            if (placed == 0 && intro >= 0) kind = intro;
        }
        float r = SPECS[kind].r;
        vector p = spawn_point(g, r);
        g->en[i] = (enemy){
            .alive = 1, .kind = kind, .p = p, .v = {0, 0, 0},
            .face = vector_normalize((vector){g->pl.p.x - p.x, 0, g->pl.p.z - p.z}),
            .hp = SPECS[kind].hp,
            .spawn_t = -0.4f * (float)placed,
            .stuck_at = p,
        };
        placed++;
    }
    char buf[32];
    if (intro >= 0) snprintf(buf, sizeof buf, "NEW: %s", SPECS[intro].name);
    else snprintf(buf, sizeof buf, "WAVE %d", g->wave);
    banner(g, buf);
}

/* ---------------------------------------------------------- Damage */

static int kill_points(const game *g, int kind) {
    /* Rounded to tens so scores stay readable. */
    return 10 * (int)lroundf((float)SPECS[kind].score * mag_factor(g->mag) / 10.0f);
}

void enemy_damage(game *g, enemy *e, vector at) {
    e->flash = 1.0f;
    if (--e->hp > 0) return;
    e->dying = 1e-4f;
    g->kills++;
    g->score += kill_points(g, e->kind);
    play_at(g, SFX_POP, at, 1.0f, 1.25f - 0.25f * SPECS[e->kind].r);
    burst(g, e->p, SPECS[e->kind].c1, SPECS[e->kind].pop_bits, 7.0f);
}

static void hurt_player(game *g, enemy *e, vector dir, float damage, float shove) {
    g->pl.hp -= damage;
    g->pl.invuln = INVULN_TIME;
    g->pl.hurt_flash = 1.0f;
    g->pl.v = vector_add(g->pl.v, vector_scale(dir, shove));
    e->v = vector_scale(dir, -4.0f);
    game_sfx(SFX_HURT, 0.9f, 0.0f, 1.0f);
    if (g->pl.hp <= 0) {
        g->pl.hp = 0;
        g->game_over = 1;
        banner(g, "GAME OVER");
        g->banner_t = 1e9f;
    }
}

/* ------------------------------------------------------------- Eater */

/* Balls inside are disabled bodies like the ones in your hand; which
 * slots come back doesn't matter, only how many. Out at the surface,
 * flung outwards (or at the player with `spit`). */
void enemy_release(game *g, enemy *e, int n, int spit) {
    float r = enemy_size(e);
    for (int k = 0; k < g->mag && n > 0 && e->eaten > 0; k++) {
        if (ball_get(g, k).enabled) continue;
        vector d;
        if (spit) {
            d = vector_normalize((vector){g->pl.p.x - e->p.x, 0, g->pl.p.z - e->p.z});
            d = vector_normalize(vector_add(d, (vector){frange(-0.2f, 0.2f), 0.5f, frange(-0.2f, 0.2f)}));
        } else {
            float a = frange(0.0f, 6.2831853f);
            d = vector_normalize((vector){cosf(a), frange(0.3f, 1.0f), sinf(a)});
        }
        vector p = vector_add(e->p, vector_scale(d, r + BALL_R + 0.05f));
        if (p.y < BALL_R + 0.01f) p.y = BALL_R + 0.01f;
        clamp_arena(g, &p, BALL_R);
        push_out_static(g, &p, BALL_R);
        physics_world_set_enabled(g->phys, BALL_ID(k), 1);
        physics_world_set_position(g->phys, BALL_ID(k), p);
        physics_world_set_velocity(g->phys, BALL_ID(k), vector_scale(d, spit ? 7.0f : frange(4.0f, 8.0f)));
        e->eaten--;
        n--;
    }
}

/* A ball lying (or rolling slowly) on the floor: what an eater wants. */
static int edible(physics_body b) {
    return b.enabled && b.position.y <= 0.5f && vector_magnitude(b.velocity) <= EATER_EAT_SPEED;
}

/* With room left, go for the nearest edible ball instead of the player. */
static void eater_goal(game *g, enemy *e, en_goal *goal) {
    if (e->eaten >= SPECS[e->kind].cap) return;
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < g->mag; k++) {
        physics_body b = ball_get(g, k);
        if (!edible(b)) continue;
        float dx = b.position.x - e->p.x, dz = b.position.z - e->p.z, d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = k; }
    }
    if (best < 0) return;
    goal->p = ball_get(g, best).position;
    vector gt = {goal->p.x - e->p.x, 0, goal->p.z - e->p.z};
    float gl = sqrtf(gt.x * gt.x + gt.z * gt.z);
    if (gl > 1e-4f) goal->dir = vector_scale(gt, 1.0f / gl);
    goal->field = &g->to_ball;
}

/* Nothing in hand, nothing loose, no grenade in the air: everything is
 * inside eaters. */
static int ammo_locked_out(const game *g) {
    if (g->ammo > 0 || g->gren.state == GREN_FLYING) return 0;
    for (int k = 0; k < g->mag; k++) if (ball_get(g, k).enabled) return 0;
    return 1;
}

/* Swallow what it rolls over; when that's all the ammo there is, spit one
 * back every EATER_BURP s so the game can go on. */
static void eater_after_move(game *g, enemy *e, float dt, int *locked) {
    float reach = enemy_size(e) + BALL_R + 0.1f;
    for (int k = 0; k < g->mag && e->eaten < SPECS[e->kind].cap; k++) {
        physics_body b = ball_get(g, k);
        if (!edible(b)) continue;
        float dx = b.position.x - e->p.x, dz = b.position.z - e->p.z;
        if (dx * dx + dz * dz > reach * reach) continue;
        physics_world_set_enabled(g->phys, BALL_ID(k), 0);
        e->eaten++;
        play_at(g, SFX_PICKUP, e->p, 0.8f, 0.55f + 0.02f * (float)e->eaten);
    }
    if (*locked < 0) *locked = ammo_locked_out(g);
    if (*locked && e->eaten > 0) {
        if ((e->burp_t += dt) >= EATER_BURP) {
            e->burp_t = 0.0f;
            enemy_release(g, e, 1, 1);
            play_at(g, SFX_POP, e->p, 0.6f, 1.6f);
            *locked = 0;
        }
    } else {
        e->burp_t = 0.0f;
    }
}

static void eater_die(game *g, enemy *e) {
    enemy_release(g, e, e->eaten, 0);
}

/* ----------------------------------------------------------- Charger */

/* Approach; with a clear line at mid range stop and wind up (aiming until
 * the last moment), then dash straight; a wall leaves it dazed. */
static int charger_steer(game *g, enemy *e, float dt, float dist, vector dir, vector *want) {
    e->st_t -= dt;
    e->cool -= dt;
    switch (e->st) {
    case CH_APPROACH:
        if (e->cool <= 0 && dist > 3.0f && dist < 14.0f && !g->game_over &&
            (g->lv->n_boxes == 0 || level_clear_line(g->lv, e->p, g->pl.p, 0.0f))) {
            e->st = CH_WINDUP;
            e->st_t = CHARGE_WINDUP;
            play_at(g, SFX_DRY, e->p, 1.0f, 0.5f);
        }
        return 0;
    case CH_WINDUP:
        *want = (vector){0, 0, 0};
        e->dash = dir;
        e->flash = fmaxf(e->flash, 0.5f + 0.5f * sinf(g->t * 40.0f));
        if (e->st_t <= 0) {
            e->st = CH_DASH;
            e->st_t = CHARGE_TIME;
            play_at(g, SFX_SHOOT, e->p, 1.0f, 0.45f);
        }
        return 0;
    case CH_DASH:
        if (e->st_t <= 0) { e->st = CH_APPROACH; e->cool = CHARGE_COOL; }
        *want = vector_scale(e->dash, CHARGE_SPEED);
        return 1;   /* the last frame too */
    case CH_DAZED:
        *want = (vector){0, 0, 0};
        if (e->st_t <= 0) { e->st = CH_APPROACH; e->cool = CHARGE_COOL; }
        return 0;
    }
    return 0;
}

static void charger_bump(game *g, enemy *e, float r) {
    e->st = CH_DAZED;
    e->st_t = CHARGE_DAZE;
    e->v = vector_scale(e->dash, -2.0f);
    play_at(g, SFX_WALL, e->p, 1.0f, 0.5f);
    burst(g, vector_add(e->p, vector_scale(e->dash, r)), (scene_color){255, 230, 120}, 10, 5.0f);
}

/* Dashing or dazed, it looks the way it went. */
static vector charger_look(const enemy *e, int rushing, vector goal_dir) {
    return rushing || e->st == CH_DAZED ? e->dash : goal_dir;
}

/* A dash hits harder and ends on the hit. */
static void charger_touch(game *g, enemy *e, vector dir, int rushing) {
    const en_spec *s = &SPECS[e->kind];
    hurt_player(g, e, dir, rushing ? CHARGE_DAMAGE : s->damage, rushing ? 16.0f : 9.0f);
    if (rushing) { e->st = CH_APPROACH; e->cool = CHARGE_COOL; }
}

/* ---------------------------------------------------------- Splitter */

static void splitter_die(game *g, enemy *e) {
    const float r = SPECS[EN_SHARD].r;
    int made = 0;
    for (int i = 0; i < MAX_ENEMIES && made < SPLIT_SHARDS; i++) {
        enemy *o = &g->en[i];
        if (o->alive) continue;
        float a = 6.2831853f * ((float)made + frange(0.0f, 0.5f)) / SPLIT_SHARDS;
        vector d = {cosf(a), 0, sinf(a)};
        vector p = vector_add(e->p, vector_scale(d, 0.5f));
        p.y = r;
        clamp_arena(g, &p, r);
        push_out_static(g, &p, r);
        *o = (enemy){
            .alive = 1, .kind = EN_SHARD, .p = p, .v = vector_scale(d, 6.0f), .face = d,
            .hp = SPECS[EN_SHARD].hp, .spawn_t = SPAWN_TIME, .stuck_at = p,
        };
        made++;
    }
}

/* -------------------------------------------------------------- Table */

const en_spec SPECS[EN_KINDS] = {
    [EN_SMALL] = {.r = 0.55f, .speed = 4.2f, .mass = 5.0f, .damage = 10.0f, .hp = 3, .score = 1000,
                  .c1 = {255, 160, 40}, .c2 = {120, 50, 10}, .pop_bits = 14},
    [EN_MED]   = {.r = 0.85f, .speed = 2.8f, .mass = 14.0f, .damage = 20.0f, .hp = 7, .score = 2500,
                  .c1 = {230, 45, 80}, .c2 = {90, 10, 30}, .pop_bits = 20},
    [EN_BIG]   = {.r = 1.30f, .speed = 1.8f, .mass = 40.0f, .damage = 30.0f, .hp = 16, .score = 6000,
                  .c1 = {150, 70, 230}, .c2 = {45, 15, 80}, .pop_bits = 26},
    [EN_EATER] = {.r = 0.75f, .speed = 3.2f, .mass = 10.0f, .damage = 15.0f, .hp = 6, .score = 2000,
                  .c1 = {110, 220, 70}, .c2 = {30, 90, 20}, .pop_bits = 20,
                  .name = "EATER", .first_wave = EATER_WAVE, .cap = EATER_CAP,
                  .grow = EATER_GROW, .slow = 0.025f, .decor = DECOR_STUDS,
                  .goal = eater_goal, .after_move = eater_after_move, .die = eater_die},
    [EN_CHARGER] = {.r = 0.70f, .speed = 2.6f, .mass = 12.0f, .damage = 15.0f, .hp = 6, .score = 3000,
                  .c1 = {235, 60, 40}, .c2 = {90, 15, 10}, .pop_bits = 20,
                  .name = "CHARGER", .first_wave = CHARGER_WAVE, .decor = DECOR_HORN,
                  .steer = charger_steer, .bump = charger_bump, .look = charger_look,
                  .touch = charger_touch},
    [EN_SPLITTER] = {.r = 1.00f, .speed = 2.4f, .mass = 18.0f, .damage = 20.0f, .hp = 6, .score = 2500,
                  .c1 = {60, 200, 210}, .c2 = {15, 70, 90}, .pop_bits = 20,
                  .name = "SPLITTER", .first_wave = SPLITTER_WAVE, .decor = DECOR_CRACKS,
                  .die = splitter_die},
    [EN_SHARD] = {.r = 0.45f, .speed = 4.6f, .mass = 3.0f, .damage = 8.0f, .hp = 1, .score = 500,
                  .c1 = {140, 240, 240}, .c2 = {30, 90, 110}, .pop_bits = 20},   /* from splitters only */
};

/* ------------------------------------------------------------ Update */

/* Rising out of the floor; 1 while still rising. */
static int rise(enemy *e, float r, float dt) {
    if (e->spawn_t >= SPAWN_TIME) return 0;
    e->spawn_t += dt;
    float k = fmaxf(0.0f, e->spawn_t) / SPAWN_TIME;
    if (k > 1) k = 1;
    k = 1 - (1 - k) * (1 - k);
    e->p.y = -r + 2.0f * r * k;
    return 1;
}

/* Velocity towards `want` (or straight to it when rushing), then out of
 * the walls, bouncing off a little so knockback reads. 1 if it touched a
 * wall or pillar. */
static int enemy_move(game *g, enemy *e, vector want, int rushing, float r, float dt) {
    if (rushing) {
        e->v = want;
    } else {
        vector dv = vector_sub(want, e->v);
        dv.y = 0;
        float dvl = vector_magnitude(dv), maxdv = ENEMY_ACCEL * dt;
        if (dvl > maxdv) dv = vector_scale(dv, maxdv / dvl);
        e->v = vector_add(e->v, dv);
    }
    e->v.y = 0;
    e->p = vector_add(e->p, vector_scale(e->v, dt));

    float lx = g->lv->half_x - r, lz = g->lv->half_z - r;
    int hit = 0;
    if (e->p.x >  lx) { e->p.x =  lx; e->v.x = -fabsf(e->v.x) * 0.4f; hit = 1; }
    if (e->p.x < -lx) { e->p.x = -lx; e->v.x =  fabsf(e->v.x) * 0.4f; hit = 1; }
    if (e->p.z >  lz) { e->p.z =  lz; e->v.z = -fabsf(e->v.z) * 0.4f; hit = 1; }
    if (e->p.z < -lz) { e->p.z = -lz; e->v.z =  fabsf(e->v.z) * 0.4f; hit = 1; }
    vector n = push_out_static(g, &e->p, r);
    if (n.x != 0 || n.z != 0) {
        float vn = e->v.x * n.x + e->v.z * n.z;
        if (vn < 0) { e->v.x -= vn * n.x; e->v.z -= vn * n.z; }   /* slide */
        hit = 1;
    }
    return hit;
}

/* Watchdog for levels with walls: an enemy that hasn't got 1 m anywhere
 * in 6 s and can't see the player (wedged in a gap too narrow for it,
 * say) sinks and rises again somewhere else. 1 if it did. */
static int unstick(game *g, enemy *e, float r, float dt) {
    if (g->lv->n_boxes == 0) return 0;
    float mx = e->p.x - e->stuck_at.x, mz = e->p.z - e->stuck_at.z;
    if (mx * mx + mz * mz > 1.0f) {
        e->stuck_at = e->p;
        e->stuck_t = 0.0f;
        return 0;
    }
    if ((e->stuck_t += dt) <= 6.0f || level_clear_line(g->lv, e->p, g->pl.p, 0.0f)) return 0;
    e->p = spawn_point(g, r);
    e->p.y = -r;
    e->v = (vector){0, 0, 0};
    e->spawn_t = 0.0f;
    e->stuck_at = e->p;
    e->stuck_t = 0.0f;
    e->st = 0;   /* per-kind state back to the start */
    return 1;
}

static void separate(game *g, int i, float r) {
    enemy *e = &g->en[i];
    for (int k = 0; k < MAX_ENEMIES; k++) {
        enemy *o = &g->en[k];
        if (k == i || !o->alive || o->dying > 0 || o->spawn_t < SPAWN_TIME) continue;
        float dx = e->p.x - o->p.x, dz = e->p.z - o->p.z;
        float d = sqrtf(dx * dx + dz * dz), min = r + enemy_size(o);
        if (d < min && d > 1e-4f) {
            float push = (min - d) * 0.5f;
            e->p.x += dx / d * push;
            e->p.z += dz / d * push;
        }
    }
}

void update_enemies(game *g, float dt) {
    int locked = -1;   /* see en_spec.after_move */
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
                if (s->die) s->die(g, e);
            }
            continue;
        }
        float r = enemy_size(e);
        if (rise(e, r, dt)) continue;
        e->p.y = r;

        vector to = {g->pl.p.x - e->p.x, 0, g->pl.p.z - e->p.z};
        float dist = sqrtf(to.x * to.x + to.z * to.z);
        vector dir = dist > 1e-4f ? vector_scale(to, 1.0f / dist) : (vector){0, 0, 1};

        /* Head for the goal: straight when nothing is in the way, along the
         * nav grid around walls otherwise. */
        en_goal goal = {g->pl.p, dir, &g->to_player[e->kind]};
        if (s->goal) s->goal(g, e, &goal);
        vector head = goal.dir;
        if (g->lv->n_boxes > 0 && !level_clear_line(g->lv, e->p, goal.p, 0.0f)) {
            nav_grid *ng = &g->nav[e->kind];
            nav_flow(ng, goal.field, goal.p);
            vector nd = nav_dir(ng, goal.field, e->p);
            if (nd.x != 0 || nd.z != 0) head = nd;
        }

        /* Slight weave so a crowd doesn't move in lockstep. */
        float weave = 0.35f * sinf(g->t * 1.3f + (float)i * 2.1f);
        vector side = {-head.z, 0, head.x};
        vector want = vector_normalize(vector_add(head, vector_scale(side, weave)));
        float speed = s->speed * (1.0f + 0.04f * (float)(g->wave - 1));
        speed *= 1.0f - s->slow * (float)e->eaten;
        if (g->game_over) speed *= 0.3f;
        want = vector_scale(want, speed);
        int rushing = s->steer ? s->steer(g, e, dt, dist, dir, &want) : 0;

        int hit_wall = enemy_move(g, e, want, rushing, r, dt);
        if (rushing && hit_wall && s->bump) s->bump(g, e, r);
        if (unstick(g, e, r, dt)) continue;
        separate(g, i, r);
        if (s->after_move) s->after_move(g, e, dt, &locked);

        /* The eye tracks what it's after. */
        vector look = s->look ? s->look(e, rushing, goal.dir) : goal.dir;
        e->face = vector_normalize(vector_add(e->face, vector_scale(look, dt * 5.0f)));

        if (!g->game_over && dist < r + PLAYER_R && g->pl.invuln <= 0.0f) {
            if (s->touch) s->touch(g, e, dir, rushing);
            else hurt_player(g, e, dir, s->damage, 9.0f);
        }
    }
}
