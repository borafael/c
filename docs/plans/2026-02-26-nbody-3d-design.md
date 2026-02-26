# N-Body 3D Simulation Design

## Goal

Extend the n-body simulation from 2D to 3D: full 3D physics with a fake-3D
rendering approach using perspective size scaling on the existing SDL2 2D
renderer. No OpenGL or GPU rendering.

## Approach

Approach 1 (Minimal 3D): extend vector math to 3D, update physics, render with
CPU-side perspective projection and painter's algorithm depth sorting. Orbital
camera controlled by existing keys.

## Vector Math (libs/math/vector.h)

The `vector` struct gains a z component:

```c
typedef struct {
    float x, y, z;
} vector;
```

All operations updated to include z:
- `vector_add`, `vector_sub`, `vector_scale` -- add z component
- `vector_magnitude` -- `sqrt(x^2 + y^2 + z^2)`
- `vector_dot` -- `x1*x2 + y1*y2 + z1*z2` (also fixes existing comma-operator bug)
- `vector_cross` -- returns a `vector` (proper 3D cross product) instead of scalar

## Orbital Camera

Three parameters:
- **azimuth** -- horizontal angle (radians), controlled by left/right arrow keys
- **elevation** -- vertical angle (radians), controlled by up/down arrow keys,
  clamped to ~(-89deg, +89deg) to avoid gimbal singularity
- **distance** -- distance from origin, controlled by +/- keys (replaces zoom)

Camera position in world space from spherical coordinates:

```c
cam_x = distance * cos(elevation) * sin(azimuth)
cam_y = distance * sin(elevation)
cam_z = distance * cos(elevation) * cos(azimuth)
```

Projection per entity:
1. Translate: subtract camera position from entity world position
2. Rotate: apply inverse camera rotation (-azimuth around Y, then -elevation
   around X) to get view-space coordinates
3. Perspective divide: `screen_x = vx / vz`, `screen_y = vy / vz` (scaled to
   screen dimensions)
4. Size scaling: `radius = base_radius / vz` (closer = bigger)

Entities with `vz <= 0` (behind camera) are culled.

## Controls

| Key         | Function              |
|-------------|-----------------------|
| Left/Right  | Azimuth rotation      |
| Up/Down     | Elevation rotation    |
| +/-         | Distance to center    |
| F/S         | Speed up/slow down    |
| R           | Reset simulation      |
| ESC         | Quit                  |

## Physics Changes

**Force calculation** -- no structural change. Vector operations are
dimension-agnostic; the O(n^2) pairwise gravity, thread-local buffers, and
merge logic all stay the same.

**Spawning** -- entities spawn inside a sphere using rejection sampling: generate
random point in a cube, reject if outside sphere. Initial velocities are small
random 3D vectors.

**Boundary collision** -- 2D rectangle bounce becomes sphere bounce: if
`magnitude(position) > world_radius`, reflect velocity off the sphere surface
normal (normalized position vector).

**World dimensions** -- `world_width` / `world_height` collapse into a single
`world_radius` parameter.

## Rendering Changes

Per-frame flow:
1. Compute camera position from (azimuth, elevation, distance)
2. For each active entity: transform to view-space, cull if behind camera,
   perspective divide for screen coords, compute mass-based radius scaled by
   depth
3. Sort visible entities by depth descending (farthest first) using `qsort`
4. Draw back-to-front with existing `render_circle()`

Sorting cost: O(n log n) for ~8000 entities is negligible vs O(n^2) forces.

A projection helper function encapsulates the transform: takes 3D world position
+ camera state, returns screen x, y, depth, or marks as culled.

**Unchanged:** `render_circle()`, color logic, `render_init/cleanup/clear/present`.

## Files Modified

- `libs/math/vector.h` -- 3D vector type and operations
- `apps/nbody/nbody.c` -- spherical spawn, sphere boundaries, camera state,
  projection + depth sort
- `apps/nbody/nbody.h` -- config struct (world_radius), camera fields
- `apps/nbody/main.c` -- CLI params and help text

## Files Unchanged

- `libs/thread/thread_pool.c/h` -- task-agnostic
- `apps/nbody/render.c/h` -- low-level SDL2 drawing unchanged
- `apps/nbody/input.c` -- same keys, different semantics

## Out of Scope

- OpenGL / GPU rendering
- Pan / focal point shifting (camera always orbits origin)
- Opacity or color-based depth cues
- Spatial partitioning optimization
