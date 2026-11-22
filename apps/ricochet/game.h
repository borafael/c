#ifndef RICOCHET_GAME_H
#define RICOCHET_GAME_H

/* Ricochet's game state and rules (game.c). Everything here runs
 * headless: the window, menus and rendering live elsewhere and only read
 * the game or feed it input. */

#include "physics.h"
#include "scene.h"
#include "vector.h"

#include <stdint.h>

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
#define PITCH_LIMIT   1.40f
#define PLAYER_HP     100.0f
#define INVULN_TIME   0.9f

/* Ammo / balls */
#define DEFAULT_AMMO  40       /* magazine size; --ammo N overrides */
#define AMMO_CAP      512      /* hard limit for --ammo (ball pairs are O(n^2)) */
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
#define SLEEP_SPEED   0.10f    /* rolling slower than this: ball comes to rest */
#define BALL_MASS     1.0f
#define MAX_SUBSTEP_DIST 0.08f /* m a ball may travel per physics substep */
#define MAX_SUBSTEPS  64

/* Enemies */
#define MAX_ENEMIES   24
#define SPAWN_TIME    0.9f     /* rise out of the floor */
#define DIE_TIME      0.30f
#define ENEMY_ACCEL   7.0f
#define KNOCKBACK     0.6f     /* scales momentum transfer for feel */
#define WAVE_GAP      2.5f
#define MAX_START_WAVE 50      /* enemy count caps at wave 22; speed keeps rising */

#define MAX_PARTS     160

/* Pillars are physics columns: balls bounce off them, everything else is
 * pushed out of them. */
#define N_PILLARS 8
extern const physics_column PILLARS[N_PILLARS];

typedef enum { EN_SMALL, EN_MED, EN_BIG, EN_KINDS } en_kind;
typedef struct {
    float r, speed, mass, damage;
    int   hp, score;
    scene_color c1, c2;
} en_spec;

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
    /* Balls (dynamic) and enemies (kinematic) are bodies in one physics
     * world, with fixed IDs: enemy i is body i, ball k is BALL_ID(k).
     * A ball in your hand is a disabled body. */
    physics_world *phys;
    int      mag;                /* balls that exist: in hand + in world */
    int      ammo;               /* balls in hand */
    enemy    en[MAX_ENEMIES];
    particle parts[MAX_PARTS];
    int      wave, kills, score, shots, hits;
    float    wave_timer;         /* countdown to next wave when > 0 */
    float    banner_t;
    char     banner[48];
    int      game_over;
    int      start_wave, start_ammo; /* settings this game began with */
    int      hs_rank;            /* HS_UNRECORDED, -1 (not ranked) or 0.. */
    float    t;
    /* Listener frame for spatial audio (updated each frame). */
    vector   ear, ear_right;
} game;

#define BALL_ID(k) (MAX_ENEMIES + (k))
#define HS_UNRECORDED (-2)

/* Gun-local frame: origin at the receiver, x = right, y = up, z = forward
 * (the camera basis). Recoil pushes it back and up, walking sways it. The
 * viewmodel and the projectile spawn both use it so balls leave the
 * actual muzzle. */
typedef struct { vector o, x, y, z; } gun_frame;

#define GUN_MUZZLE_Z 0.42f
#define GUN_MUZZLE_Y 0.012f

extern const en_spec SPECS[EN_KINDS];

/* What the next game_reset starts with (command line / options menu).
 * start_ammo < 0 means a full magazine. */
typedef struct {
    int mag, start_ammo, start_wave;
} game_settings;
extern game_settings game_cfg;

/* Scales every game sound (the title screen's background match plays
 * quietly). Menu clicks call sfx_play directly and ignore it. */
extern float game_volume;

/* Shared xorshift RNG: the simulation is deterministic for a seed. */
void  rng_seed(uint64_t seed);
float frand(void);
float frange(float a, float b);

void game_reset(game *g);
void game_update(game *g, const input *in, float dt);
/* Change the magazine mid-game (never a free refill). */
void game_set_magazine(game *g, int mag);

physics_body ball_get(const game *g, int k);
vector cam_forward(float yaw, float pitch);
vector eye_pos(const game *g);
gun_frame gun_frame_make(const game *g, vector eye, vector f, vector r, vector u);
vector gun_local(const gun_frame *gf, float x, float y, float z);
int   enemies_left(const game *g);
float enemy_radius(const enemy *e);
/* Kill-points multiplier for a magazine size. */
float mag_factor(int mag);

#endif
