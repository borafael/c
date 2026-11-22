#include "bot.h"

#include <math.h>
#include <string.h>

/* Autopilot: aims (with gravity drop compensation) at the nearest enemy,
 * fires short bursts, and goes hunting for balls once the magazine runs
 * low (with hysteresis so it refills properly before fighting again).
 * While collecting it still turns and fights any enemy inside
 * BOT_THREAT_DIST as long as it holds a ball — otherwise a low start
 * (--start-ammo) has it calmly gathering while it is eaten alive. */
#define BOT_THREAT_DIST 9.0f
void bot_input(const game *g, input *in, float dt) {
    static float burst_clock = 0.0f;
    static int collecting = 0;
    memset(in, 0, sizeof *in);
    burst_clock += dt;
    const player *pl = &g->pl;
    vector eye = eye_pos(g);

    int out_in_world = g->mag - g->ammo;
    if (g->ammo == 0 || (enemies_left(g) == 0 && out_in_world > 0)) collecting = 1;
    if (g->ammo >= g->mag * 3 / 4 || out_in_world == 0) collecting = 0;

    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const enemy *e = &g->en[i];
        if (!e->alive || e->dying > 0 || e->spawn_t < SPAWN_TIME) continue;
        float d = vector_magnitude(vector_sub(e->p, eye));
        if (d < bd) { bd = d; best = i; }
    }
    int threatened = best >= 0 && bd < BOT_THREAT_DIST && g->ammo > 0;

    if (collecting && !threatened) {
        int nearest = -1;
        float nd = 1e9f;
        for (int i = 0; i < g->mag; i++) {
            physics_body b = ball_get(g, i);
            if (!b.enabled || b.age < PICKUP_MIN_AGE) continue;
            float dx = b.position.x - pl->p.x, dz = b.position.z - pl->p.z, d = dx * dx + dz * dz;
            if (d < nd) { nd = d; nearest = i; }
        }
        if (nearest >= 0) {
            vector bp = ball_get(g, nearest).position;
            float want = atan2f(bp.x - pl->p.x, bp.z - pl->p.z);
            float dy = remainderf(want - pl->yaw, 2.0f * (float)M_PI);
            in->look_dx = fmaxf(-4 * dt, fminf(4 * dt, dy));
            in->look_dy = (-0.25f - pl->pitch) * fminf(1, dt * 5);
            in->move_f = 1.0f;
            in->sprint = 1;
        }
        return;
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
