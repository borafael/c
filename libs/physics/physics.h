#ifndef PHYSICS_H
#define PHYSICS_H

#include "vector.h"

/**
 * Thread-pooled pairwise-gravity N-body world.
 *
 * A physics_world owns a pool of bodies with position, velocity,
 * acceleration, and mass. Each step accumulates pairwise gravitational
 * attraction in parallel, integrates with semi-implicit Euler, and
 * optionally merges bodies that come within `softening` or reflects
 * them off a spherical boundary.
 *
 * Body IDs are stable: once returned by physics_world_add_body, an ID
 * keeps referring to the same body until that body is removed (or
 * absorbed by a merge), after which the slot can be reused by a later
 * add.
 */
typedef struct physics_world physics_world;

typedef struct {
    int   max_bodies;       /* Maximum bodies alive at once */
    int   num_threads;      /* Worker threads used for force accumulation */
    float gravity;          /* Gravitational constant G */
    float dt;               /* Integration time step */
    float softening;        /* Pairs closer than this skip forces */
    int   merge_on_contact; /* Non-zero: momentum-conserving merge when dist < softening */
    int   bounded;          /* Non-zero: reflect bodies inside a sphere */
    float world_radius;     /* Boundary sphere radius when bounded */
} physics_config;

/**
 * Return a sensible default config (300 bodies, 4 threads, G=0.5,
 * dt=0.016, softening=5, merge_on_contact=1, unbounded).
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
 * Add a body. Returns body ID >= 0, or -1 if the world is full.
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
 * Advance the world by one `dt` (force accumulation, merges,
 * integration, boundary reflection).
 */
void physics_world_step(physics_world *world);

/**
 * Total ID capacity. Iterate [0, capacity) and check body_alive.
 */
int physics_world_capacity(const physics_world *world);

/**
 * Number of currently-alive bodies.
 */
int physics_world_body_count(const physics_world *world);

int    physics_world_body_alive(const physics_world *world, int id);
vector physics_world_body_position(const physics_world *world, int id);
vector physics_world_body_velocity(const physics_world *world, int id);
float  physics_world_body_mass(const physics_world *world, int id);

#endif /* PHYSICS_H */
