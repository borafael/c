#ifndef PHYSICS_H
#define PHYSICS_H

#include "vector.h"

/**
 * Sphere-body physics world.
 *
 * A physics_world owns a pool of bodies — point masses that may also have
 * a radius — and advances them with semi-implicit Euler. Every feature
 * below is opt-in through physics_config, so one API serves very
 * different simulations:
 *
 *   Mutual gravity  pairwise attraction (G = `gravity`), accumulated in
 *                   parallel on a thread pool; optional momentum-
 *                   conserving merge when two bodies come within
 *                   `softening`; optional reflective boundary sphere.
 *                   (apps/nbody)
 *   Falling         uniform downward gravity (`fall_gravity`, along -y).
 *   Collisions      spheres bounce off a ground plane (with grip, rolling
 *                   friction and sleep), the vertical walls of a box,
 *                   vertical columns, kinematic bodies and each other,
 *                   with impulse responses and restitution. Adaptive
 *                   substeps keep fast bodies from tunnelling.
 *                   (apps/ricochet)
 *
 * Body types:
 *   DYNAMIC    moved by the world (gravity, collisions).
 *   KINEMATIC  moved by you (set its position / velocity each frame); the
 *              world never changes it, but dynamic spheres collide with it
 *              using its velocity and mass. Each such contact is reported
 *              with the impulse the dynamic body received, so the caller
 *              can push the kinematic body back however it sees fit.
 *
 * Restitution of a contact is the minimum of the two surfaces' values
 * (body vs body, or body vs ground / wall / column).
 *
 * Body IDs are stable: an ID keeps referring to the same body until it is
 * removed (or absorbed by a merge); the slot may then be reused by a later
 * add. A body can also be disabled — kept with its ID but ignored by the
 * simulation — and enabled again later.
 *
 * Bodies are processed in ID order and nothing is threaded except mutual
 * gravity, so a world without it is deterministic: the same calls give
 * bit-identical results.
 */

typedef struct physics_world physics_world;

typedef enum {
    PHYSICS_DYNAMIC   = 0,
    PHYSICS_KINEMATIC = 1,
} physics_body_type;

/** Vertical column, infinitely tall: a circle of radius `r` at (x, z). */
typedef struct {
    float x, z, r;
    float restitution;
} physics_column;

typedef enum {
    PHYSICS_CONTACT_GROUND, /* other = -1 */
    PHYSICS_CONTACT_WALL,   /* other = 0 +x, 1 -x, 2 +z, 3 -z */
    PHYSICS_CONTACT_COLUMN, /* other = column index */
    PHYSICS_CONTACT_BODY,   /* other = the other body's ID */
} physics_contact_kind;

/**
 * One collision, reported after the world has resolved it. `body` is
 * always dynamic. `normal` points from the other surface towards `body`;
 * `speed` is how fast they were approaching along it (> 0); `impulse` is
 * the impulse `body` received along `normal` (body and column contacts;
 * 0 for ground / wall).
 */
typedef struct {
    physics_contact_kind kind;
    int    body;
    int    other;
    vector normal;
    float  speed;
    float  impulse;
} physics_contact;

/**
 * Called for every contact during physics_world_advance. It may change
 * the velocity or radius of kinematic bodies (e.g. knock one back, or
 * grow one that is dying); it must not add, remove, enable or disable
 * bodies.
 */
typedef void (*physics_contact_fn)(void *user, physics_world *world,
                                   const physics_contact *contact);

typedef struct {
    int   max_bodies;       /* Maximum bodies alive at once */
    float dt;               /* Time step of physics_world_step */

    /* Mutual gravity (off when gravity == 0 and merge_on_contact == 0). */
    int   num_threads;      /* Worker threads used for force accumulation */
    float gravity;          /* Gravitational constant G */
    float softening;        /* Pairs closer than this skip forces */
    int   merge_on_contact; /* Non-zero: momentum-conserving merge when dist < softening */
    int   bounded;          /* Non-zero: reflect bodies inside a sphere */
    float world_radius;     /* Boundary sphere radius when bounded */

    /* Falling. */
    float fall_gravity;     /* Downward (-y) acceleration; 0 = off */

    /* Collisions between spheres and with the static colliders below. */
    int   collisions;
    int   ground;              /* Non-zero: solid ground plane y = ground_y */
    float ground_y;
    float ground_restitution;
    float ground_grip;         /* Fraction of horizontal speed kept per impact */
    float ground_settle_speed; /* Impacts slower than this stop dead */
    float roll_friction;       /* 1/s, horizontal damping while on the ground */
    float sleep_speed;         /* Rolling slower than this: body sleeps */
    int   walls;               /* Non-zero: vertical walls bounding x and z */
    float wall_min_x, wall_max_x, wall_min_z, wall_max_z;
    float wall_restitution;
    const physics_column *columns; /* Copied at create; may be NULL */
    int   column_count;

    /* Adaptive substeps: no body moves more than this per substep. 0 = one
     * substep per advance. */
    float max_substep_dist;
    int   min_substeps;
    int   max_substeps;

    physics_contact_fn on_contact; /* May be NULL */
    void *contact_user;
} physics_config;

/** Everything about one body, as returned by physics_world_body. */
typedef struct {
    int    alive;     /* ID in use */
    int    enabled;   /* Simulated (alive and not disabled) */
    physics_body_type type;
    vector position;
    vector velocity;
    float  mass;
    float  radius;
    float  restitution;
    int    sleeping;  /* Resting on the ground; woken by contacts */
    float  age;       /* Seconds simulated since added / enabled */
} physics_body;

/** Description for physics_world_add. */
typedef struct {
    physics_body_type type;
    vector position;
    vector velocity;
    float  mass;
    float  radius;
    float  restitution;
} physics_body_desc;

/**
 * Return a sensible default config: 300 bodies, mutual gravity on
 * (4 threads, G=0.5, softening=5, merging), dt=0.016, unbounded; falling,
 * collisions and substeps off.
 */
physics_config physics_default_config(void);

/**
 * Create a world. Returns NULL on allocation or pool-creation failure.
 */
physics_world *physics_world_create(const physics_config *config);

/**
 * Destroy a world. Safe to call with NULL.
 */
void physics_world_destroy(physics_world *world);

/**
 * Add a body. Returns its ID (>= 0), or -1 if the world is full.
 */
int physics_world_add(physics_world *world, const physics_body_desc *desc);

/**
 * Shorthand: add a dynamic point mass (radius 0, restitution 1).
 */
int physics_world_add_body(physics_world *world, vector position,
                           vector velocity, float mass);

/**
 * Mark a body dead and return its slot to the free list. No-op if the
 * ID is out of range or already dead.
 */
void physics_world_remove_body(physics_world *world, int id);

/**
 * Remove every body. Safe to call between steps.
 */
void physics_world_clear(physics_world *world);

/**
 * Advance the world by one config `dt`.
 */
void physics_world_step(physics_world *world);

/**
 * Advance the world by `dt` seconds, in as many substeps as the
 * config's adaptive-substep settings ask for. Returns the substep count.
 */
int physics_world_advance(physics_world *world, float dt);

/**
 * Total ID capacity. Iterate [0, capacity) and check body_alive.
 */
int physics_world_capacity(const physics_world *world);

/**
 * Number of currently-alive bodies.
 */
int physics_world_body_count(const physics_world *world);

/** Snapshot of one body (all zero if the ID is out of range). */
physics_body physics_world_body(const physics_world *world, int id);

int    physics_world_body_alive(const physics_world *world, int id);
vector physics_world_body_position(const physics_world *world, int id);
vector physics_world_body_velocity(const physics_world *world, int id);
float  physics_world_body_mass(const physics_world *world, int id);

/** Setters. Changing a dynamic body's position or velocity wakes it. */
void physics_world_set_position(physics_world *world, int id, vector p);
void physics_world_set_velocity(physics_world *world, int id, vector v);
void physics_world_set_radius(physics_world *world, int id, float r);
void physics_world_set_mass(physics_world *world, int id, float mass);

/**
 * Take a body out of the simulation (keeping its ID) or put it back.
 * Enabling resets its age and wakes it.
 */
void physics_world_set_enabled(physics_world *world, int id, int enabled);

/**
 * Push a circle of radius `r` at p (x, z) out of every column it overlaps.
 * Returns the normal of the last column it was pushed out of, or a zero
 * vector. Handy for moving characters through the same static geometry.
 */
vector physics_world_push_out_columns(const physics_world *world,
                                      vector *p, float r);

#endif /* PHYSICS_H */
