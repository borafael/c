# Encapsulation Refactoring Design

## Goal

Improve encapsulation across the codebase by making the raytracer own the camera concept, introducing a viewport struct, and extracting large code blocks into named functions.

## Approach

Option B: Camera to raytracer + function extraction. No full struct encapsulation of entity/simulation state (deferred to a future pass).

## Changes

### 1. Opaque `rt_camera` in Raytracer

Current `rt_camera` is a public struct with exposed fields. It becomes opaque.

**New API in `raytrace.h`:**

```c
typedef struct rt_camera rt_camera;

rt_camera *rt_camera_create(vector position, vector direction);
void       rt_camera_place(rt_camera *cam, vector position, vector direction);
void       rt_camera_destroy(rt_camera *cam);
```

**Hidden struct in `raytrace.c`:**

```c
struct rt_camera {
    vector origin;
    vector forward;
    vector right;
    vector up;
    float  fov_factor;
};
```

`rt_camera_create` and `rt_camera_place` compute `forward`, `right`, `up` internally from `position` + `direction`.

### 2. Public `rt_viewport` in Raytracer

New public struct (no invariants, just data):

```c
typedef struct {
    int width;
    int height;
    float fov;
} rt_viewport;
```

### 3. Updated `rt_render_chunk`

Takes `rt_viewport *` instead of separate width/height:

```c
void rt_render_chunk(uint32_t *pixel_buf, const rt_viewport *viewport,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene);
```

### 4. Function Extraction in `nbody.c`

Extract large code blocks into named static functions:

```c
static void reset_accelerations(void);
static void accumulate_forces(void);                  // sets global merge_list/merge_count
static void apply_merges(void);                       // reads global merge_list/merge_count
static void update_physics_component(int entity_id);
static void check_collision_with_boundary(int entity_id);
static rt_sphere entity_to_sphere(int entity_id);
static void ensure_render_resources(int w, int h);
static void render_scene(const rt_camera *cam, const rt_viewport *vp);
```

`merge_count` becomes a static global alongside `merge_list` for consistency.

After extraction, `nbody_step()` reads as:

```c
static void nbody_step(void) {
    reset_accelerations();
    accumulate_forces();
    apply_merges();

    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;
        update_physics_component(i);
        if (bounds_enabled) check_collision_with_boundary(i);
    }
}
```

### 5. Input Consolidation

Replace the 8 individual camera/speed functions with one:

```c
void nbody_handle_input(const input_events *events);
```

Removed from `nbody.h`:
- `nbody_distance_increase/decrease()`
- `nbody_rotate_left/right/up/down()`
- `nbody_speed_up/down()`

### 6. Camera Ownership

`nbody.c` owns an `rt_camera *` and orbital state (azimuth, elevation, distance). Updated in `nbody_handle_input`. The orbital-to-cartesian math moves into `nbody_handle_input`.

`main.c` game loop becomes:

```c
while (running) {
    input_poll(&events);
    if (events.quit) running = 0;
    nbody_handle_input(&events);
    nbody_update();
    nbody_render(screen_width, screen_height);
}
```

## What Stays the Same

- `input.h/c` — unchanged, generic input abstraction
- `render.h/c` — unchanged, SDL2 abstraction
- `thread_pool.h/c` — unchanged, already well encapsulated
- `vector.h` — unchanged, header-only math
- `rt_sphere`, `rt_scene` — unchanged
- Entity state as static globals in `nbody.c` — deferred to future refactoring

## Performance Impact

Zero. Passing struct pointers generates the same memory loads as global access. Extracted static functions are inlined by the compiler at -O2.
