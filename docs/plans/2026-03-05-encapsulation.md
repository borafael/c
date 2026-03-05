# Encapsulation Refactoring Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Improve encapsulation by making the raytracer own the camera concept, introducing a viewport struct, and extracting large code blocks in nbody.c into named functions.

**Architecture:** Camera becomes an opaque type in the raytracer library. A new public `rt_viewport` struct separates projection from camera transform. `nbody.c` functions are extracted for readability, and 8 camera/speed functions collapse into one `nbody_handle_input`.

**Tech Stack:** C, GNU Autotools, SDL2, pthreads

**Build/verify command:** `autoreconf -i && ./configure && make clean && make`

**Run command:** `./apps/nbody/nbody`

---

### Task 1: Make `rt_camera` opaque and add camera API

**Files:**
- Modify: `libs/raytrace/raytrace.h:7-19` (replace public struct with forward declaration + API)
- Modify: `libs/raytrace/raytrace.c` (add struct definition + camera functions)

**Step 1: Update `raytrace.h`**

Replace the public `rt_camera` struct (lines 13-19) with a forward declaration and API functions. Keep `rt_sphere` as-is.

```c
/* Replace lines 13-19 with: */
typedef struct rt_camera rt_camera;

/**
 * Create a camera at position, looking toward direction.
 * Computes internal orientation vectors automatically.
 */
rt_camera *rt_camera_create(vector position, vector direction);

/**
 * Reposition the camera and change its direction.
 */
void rt_camera_place(rt_camera *cam, vector position, vector direction);

/**
 * Destroy the camera and free resources.
 */
void rt_camera_destroy(rt_camera *cam);
```

**Step 2: Add camera implementation to `raytrace.c`**

Add the hidden struct definition after the existing `struct rt_scene` block (after line 11), and add the three camera functions after `rt_scene_destroy` (after line 40).

```c
struct rt_camera {
    vector origin;
    vector forward;
    vector right;
    vector up;
};

static void camera_update_orientation(rt_camera *cam) {
    cam->forward = vector_normalize(cam->forward);

    vector world_up = {0.0f, 1.0f, 0.0f};
    cam->right = vector_normalize(vector_cross(cam->forward, world_up));
    /* Handle degenerate case when looking straight up/down */
    if (vector_magnitude(cam->right) < 0.001f) {
        cam->right = (vector){1.0f, 0.0f, 0.0f};
    }
    cam->up = vector_cross(cam->right, cam->forward);
}

rt_camera *rt_camera_create(vector position, vector direction) {
    rt_camera *cam = malloc(sizeof(rt_camera));
    if (!cam) return NULL;
    cam->origin = position;
    cam->forward = direction;
    camera_update_orientation(cam);
    return cam;
}

void rt_camera_place(rt_camera *cam, vector position, vector direction) {
    cam->origin = position;
    cam->forward = direction;
    camera_update_orientation(cam);
}

void rt_camera_destroy(rt_camera *cam) {
    free(cam);
}
```

**Step 3: Build and verify it compiles**

Run: `autoreconf -i && ./configure && make clean && make`

Expected: Build succeeds. The app won't link yet because `nbody.c` still accesses `rt_camera` fields directly — that's fixed in Task 3.

**Step 4: Commit**

```bash
git add libs/raytrace/raytrace.h libs/raytrace/raytrace.c
git commit -m "refactor(raytrace): make rt_camera opaque with create/place/destroy API"
```

---

### Task 2: Add `rt_viewport` and update `rt_render_chunk` signature

**Files:**
- Modify: `libs/raytrace/raytrace.h:44-50` (add viewport struct, update render signature)
- Modify: `libs/raytrace/raytrace.c:59-120` (update render implementation)

**Step 1: Add `rt_viewport` to `raytrace.h`**

Add the viewport struct before the `rt_render_chunk` declaration:

```c
/**
 * Viewport defining projection parameters.
 */
typedef struct {
    int width;
    int height;
    float fov;
} rt_viewport;
```

Update the `rt_render_chunk` signature:

```c
/**
 * Render a chunk of scanlines [y_start, y_end) into pixel_buf.
 * pixel_buf is ARGB8888 format, viewport->width * viewport->height uint32_t's.
 * fov is in radians. Caller is responsible for parallelizing across chunks.
 */
void rt_render_chunk(uint32_t *pixel_buf, const rt_viewport *viewport,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene);
```

**Step 2: Update `rt_render_chunk` implementation in `raytrace.c`**

Change the function signature and replace `width`/`height`/`camera->fov_factor` references:

```c
void rt_render_chunk(uint32_t *pixel_buf, const rt_viewport *viewport,
                     int y_start, int y_end,
                     const rt_camera *camera, const rt_scene *scene) {
    int width = viewport->width;
    int height = viewport->height;
    /* Convert FOV angle to scaling factor */
    float fov_factor = (float)height / (2.0f * tanf(viewport->fov / 2.0f));

    /* Fixed directional light */
    vector light_dir = vector_normalize((vector){1.0f, 1.0f, -1.0f});
    float ambient = 0.15f;

    float half_w = (float)width * 0.5f;
    float half_h = (float)height * 0.5f;

    for (int y = y_start; y < y_end; y++) {
        for (int x = 0; x < width; x++) {
            /* Map pixel to normalized screen coords */
            float sx = ((float)x - half_w) / fov_factor;
            float sy = -((float)y - half_h) / fov_factor;

            /* Construct ray direction in world space */
            vector dir = vector_add(
                vector_add(
                    camera->forward,
                    vector_scale(camera->right, sx)),
                vector_scale(camera->up, sy));
            dir = vector_normalize(dir);

            /* Find closest sphere hit */
            float closest_t = FLT_MAX;
            int hit_idx = -1;

            for (int i = 0; i < scene->count; i++) {
                float t = intersect_sphere(camera->origin, dir,
                                           &scene->spheres[i]);
                if (t > 0.0f && t < closest_t) {
                    closest_t = t;
                    hit_idx = i;
                }
            }

            if (hit_idx >= 0) {
                /* Compute hit point and normal */
                const rt_sphere *sp = &scene->spheres[hit_idx];
                vector hit = vector_add(camera->origin,
                                        vector_scale(dir, closest_t));
                vector normal = vector_normalize(
                    vector_sub(hit, sp->center));

                /* Lambertian diffuse shading */
                float diffuse = vector_dot(normal, light_dir);
                if (diffuse < 0.0f) diffuse = 0.0f;
                float shade = ambient + 0.85f * diffuse;

                uint8_t cr = (uint8_t)(sp->r * shade > 255.0f ? 255 : sp->r * shade);
                uint8_t cg = (uint8_t)(sp->g * shade > 255.0f ? 255 : sp->g * shade);
                uint8_t cb = (uint8_t)(sp->b * shade > 255.0f ? 255 : sp->b * shade);

                pixel_buf[y * width + x] = (255u << 24) | (cr << 16) | (cg << 8) | cb;
            } else {
                /* Background: black */
                pixel_buf[y * width + x] = (255u << 24);
            }
        }
    }
}
```

**Step 3: Build and verify**

Run: `autoreconf -i && ./configure && make clean && make`

Expected: Raytracer library compiles. App won't link yet — `nbody.c` still uses old signature.

**Step 4: Commit**

```bash
git add libs/raytrace/raytrace.h libs/raytrace/raytrace.c
git commit -m "refactor(raytrace): add rt_viewport and update rt_render_chunk signature"
```

---

### Task 3: Update `nbody.c` to use new raytracer API (camera + viewport)

**Files:**
- Modify: `apps/nbody/nbody.c` (replace direct camera construction with API calls, add viewport)

**Step 1: Update globals**

Replace camera globals (lines 66-68) and remove `fov_factor` from `render_chunk_args`:

```c
/* Replace lines 66-68 with: */
static rt_camera *camera = NULL;
static float camera_azimuth = 0.0f;
static float camera_elevation = 0.3f;
static float camera_distance = 1500.0f;
static float time_scale = 1.0f;
```

Update `render_chunk_args` (lines 79-87) — remove `width`, `height`, `camera` fields, replace with viewport:

```c
typedef struct {
    uint32_t *pixel_buf;
    const rt_viewport *viewport;
    int y_start;
    int y_end;
    const rt_camera *camera;
    const rt_scene *scene;
} render_chunk_args;
```

**Step 2: Create camera in `nbody_init`**

At the end of `nbody_init` (after line 182, after thread pool creation), add:

```c
    vector pos = {
        camera_distance * cosf(camera_elevation) * sinf(camera_azimuth),
        camera_distance * sinf(camera_elevation),
        -camera_distance * cosf(camera_elevation) * cosf(camera_azimuth)
    };
    vector dir = vector_normalize(vector_scale(pos, -1.0f));
    camera = rt_camera_create(pos, dir);
    if (!camera) {
        fprintf(stderr, "Failed to create camera\n");
        exit(EXIT_FAILURE);
    }
```

**Step 3: Update `render_chunk_task`**

```c
static void render_chunk_task(void *arg) {
    render_chunk_args *a = (render_chunk_args *)arg;
    rt_render_chunk(a->pixel_buf, a->viewport,
                    a->y_start, a->y_end, a->camera, a->scene);
}
```

**Step 4: Destroy camera in `nbody_cleanup`**

Add to `nbody_cleanup` (after the thread pool block):

```c
    if (camera) {
        rt_camera_destroy(camera);
        camera = NULL;
    }
```

**Step 5: Update `nbody_reset`**

Add camera repositioning at the end of `nbody_reset` (after `nbody_spawn_entities()`):

```c
    if (camera) {
        vector pos = {
            camera_distance * cosf(camera_elevation) * sinf(camera_azimuth),
            camera_distance * sinf(camera_elevation),
            -camera_distance * cosf(camera_elevation) * cosf(camera_azimuth)
        };
        vector dir = vector_normalize(vector_scale(pos, -1.0f));
        rt_camera_place(camera, pos, dir);
    }
```

**Step 6: Update `nbody_render`**

Replace the entire `nbody_render` function (lines 402-480):

```c
void nbody_render(int screen_width, int screen_height) {
    int w = screen_width / RT_SCALE;
    int h = screen_height / RT_SCALE;

    /* Lazy init raytracer resources */
    if (!rt_scene_ptr) {
        rt_scene_ptr = rt_scene_create(MAX_ENTITIES);
    }
    if (!pixel_buffer || rt_width != w || rt_height != h) {
        free(pixel_buffer);
        pixel_buffer = calloc((size_t)w * h, sizeof(uint32_t));
        if (rt_texture) render_destroy_texture(rt_texture);
        rt_texture = render_create_texture(w, h);
        rt_width = w;
        rt_height = h;
    }

    rt_viewport viewport = { .width = w, .height = h, .fov = 1.5708f }; /* ~90 degrees */

    /* Populate scene with entity spheres */
    rt_scene_clear(rt_scene_ptr);
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;

        float mass = physics_components[i].mass;
        float t = logf(mass) / logf(1000.0f);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        rt_sphere sp;
        sp.center = position_components[i].coordinates;
        sp.radius = 2.0f + logf(mass) * 2.0f;
        if (sp.radius < 1.0f) sp.radius = 1.0f;
        sp.r = (uint8_t)(50 + t * 205);
        sp.g = (uint8_t)(50 * (1 - t * t));
        sp.b = (uint8_t)(255 * (1 - t * t));

        rt_scene_add_sphere(rt_scene_ptr, sp);
    }

    /* Parallel render */
    int num_chunks = num_threads;
    render_chunk_args chunk_args[MAX_THREADS];
    int rows_per_chunk = h / num_chunks;

    for (int c = 0; c < num_chunks; c++) {
        chunk_args[c].pixel_buf = pixel_buffer;
        chunk_args[c].viewport = &viewport;
        chunk_args[c].y_start = c * rows_per_chunk;
        chunk_args[c].y_end = (c == num_chunks - 1) ? h : (c + 1) * rows_per_chunk;
        chunk_args[c].camera = camera;
        chunk_args[c].scene = rt_scene_ptr;
        thread_pool_submit(pool, render_chunk_task, &chunk_args[c]);
    }
    thread_pool_wait(pool);

    /* Display */
    render_clear();
    render_texture_update(rt_texture, pixel_buffer, w * (int)sizeof(uint32_t));
    render_present();
}
```

**Step 7: Build and verify**

Run: `autoreconf -i && ./configure && make clean && make`

Expected: Compiles and links. Note: the 8 camera/speed functions in `nbody.c` still exist but now the camera variable is an `rt_camera *` — they'll need to call `rt_camera_place` to update. For now they update the orbital floats only; the camera gets repositioned per-frame in the next task.

**Step 8: Run and verify visually**

Run: `./apps/nbody/nbody -n 50`

Expected: Simulation renders correctly with spheres visible and camera works.

**Step 9: Commit**

```bash
git add apps/nbody/nbody.c
git commit -m "refactor(nbody): use rt_camera and rt_viewport APIs from raytracer"
```

---

### Task 4: Add `nbody_handle_input` and simplify `main.c`

**Files:**
- Modify: `apps/nbody/nbody.h` (replace 8 functions with `nbody_handle_input`)
- Modify: `apps/nbody/nbody.c` (consolidate input handling, add camera update)
- Modify: `apps/nbody/main.c` (simplify game loop)

**Step 1: Update `nbody.h`**

Add the `input.h` include and replace the 8 camera/speed function declarations (lines 55-93) with:

```c
#include "input.h"

/**
 * Handle input events (camera movement, speed, reset).
 */
void nbody_handle_input(const input_events *events);
```

Keep `nbody_set_bounds`, `nbody_spawn_entities`, `nbody_reset`, `nbody_update`, `nbody_render`, `nbody_cleanup`, `nbody_init`, `nbody_default_config`.

**Step 2: Replace the 8 functions in `nbody.c` with `nbody_handle_input`**

Remove the 8 individual functions (lines 105-141) and add a helper to update the camera from orbital state, plus the consolidated handler:

```c
static void update_camera_from_orbital(void) {
    vector pos = {
        camera_distance * cosf(camera_elevation) * sinf(camera_azimuth),
        camera_distance * sinf(camera_elevation),
        -camera_distance * cosf(camera_elevation) * cosf(camera_azimuth)
    };
    vector dir = vector_normalize(vector_scale(pos, -1.0f));
    rt_camera_place(camera, pos, dir);
}

void nbody_handle_input(const input_events *events) {
    if (events->reset) {
        nbody_reset();
        return;
    }

    if (events->zoom_in) {
        camera_distance /= 1.1f;
        if (camera_distance < 10.0f) camera_distance = 10.0f;
    }
    if (events->zoom_out) {
        camera_distance *= 1.1f;
        if (camera_distance > 20000.0f) camera_distance = 20000.0f;
    }
    if (events->pan_left)  camera_azimuth -= rotation_speed;
    if (events->pan_right) camera_azimuth += rotation_speed;
    if (events->pan_up) {
        camera_elevation += rotation_speed;
        if (camera_elevation > 1.5f) camera_elevation = 1.5f;
    }
    if (events->pan_down) {
        camera_elevation -= rotation_speed;
        if (camera_elevation < -1.5f) camera_elevation = -1.5f;
    }
    if (events->speed_up) {
        time_scale *= 1.5f;
        if (time_scale > 50.0f) time_scale = 50.0f;
    }
    if (events->speed_down) {
        time_scale /= 1.5f;
        if (time_scale < 0.1f) time_scale = 0.1f;
    }

    update_camera_from_orbital();
}
```

Also use `update_camera_from_orbital()` in `nbody_init` and `nbody_reset` instead of inline camera math.

In `nbody_init`, replace the camera creation block (added in Task 3) with:

```c
    camera = rt_camera_create((vector){0, 0, 0}, (vector){0, 0, -1});
    if (!camera) {
        fprintf(stderr, "Failed to create camera\n");
        exit(EXIT_FAILURE);
    }
    update_camera_from_orbital();
```

In `nbody_reset`, replace the camera update block (added in Task 3) with:

```c
    update_camera_from_orbital();
```

**Step 3: Simplify `main.c`**

Replace the game loop (lines 82-101):

```c
    int running = 1;
    while (running) {
        input_events events;
        input_poll(&events);

        if (events.quit) running = 0;
        nbody_handle_input(&events);

        nbody_update();
        nbody_render(screen_width, screen_height);
        render_delay(1);
    }
```

Remove the `#include "input.h"` from `main.c` since it's now included via `nbody.h`.

**Step 4: Build and verify**

Run: `autoreconf -i && ./configure && make clean && make`

Expected: Compiles and links cleanly.

**Step 5: Run and verify all controls work**

Run: `./apps/nbody/nbody -n 50`

Test: Arrow keys rotate, +/- zoom, F/S change speed, R resets, ESC quits.

**Step 6: Commit**

```bash
git add apps/nbody/nbody.h apps/nbody/nbody.c apps/nbody/main.c
git commit -m "refactor(nbody): consolidate 8 input functions into nbody_handle_input"
```

---

### Task 5: Extract physics functions from `nbody_step`

**Files:**
- Modify: `apps/nbody/nbody.c` (extract functions, add `merge_count` global)

**Step 1: Add `merge_count` as static global**

Near line 52 (after `merge_list`), add:

```c
static int merge_count = 0;
```

**Step 2: Extract `reset_accelerations`**

Extract lines 296-301 of `nbody_step`:

```c
static void reset_accelerations(void) {
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;
        physics_components[i].acceleration = (vector){0, 0, 0};
    }
}
```

**Step 3: Extract `accumulate_forces`**

Extract lines 303-328 of `nbody_step`:

```c
static void accumulate_forces(void) {
    int chunk = MAX_ENTITIES / num_threads;
    for (int t = 0; t < num_threads; t++) {
        task_args[t].start = t * chunk;
        task_args[t].end = (t == num_threads - 1) ? MAX_ENTITIES : (t + 1) * chunk;
        thread_pool_submit(pool, compute_forces_chunk, &task_args[t]);
    }
    thread_pool_wait(pool);

    /* Sum thread-local accelerations */
    for (int t = 0; t < num_threads; t++) {
        for (int i = 0; i < MAX_ENTITIES; i++) {
            physics_components[i].acceleration = vector_add(
                physics_components[i].acceleration, task_args[t].local_accel[i]);
        }
    }

    /* Collect merge candidates from all threads */
    merge_count = 0;
    for (int t = 0; t < num_threads; t++) {
        for (int m = 0; m < task_args[t].merge_count; m++) {
            if (merge_count < MAX_MERGES) {
                merge_list[merge_count++] = task_args[t].local_merges[m];
            }
        }
    }
}
```

**Step 4: Extract `apply_merges`**

Extract lines 330-357 of `nbody_step`:

```c
static void apply_merges(void) {
    for (int m = 0; m < merge_count; m++) {
        int i = merge_list[m].i;
        int j = merge_list[m].j;

        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;
        if ((entity_masks[j] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;

        float m_i = physics_components[i].mass;
        float m_j = physics_components[j].mass;
        float total_mass = m_i + m_j;

        position_components[i].coordinates = vector_scale(
            vector_add(vector_scale(position_components[i].coordinates, m_i),
                       vector_scale(position_components[j].coordinates, m_j)),
            1.0f / total_mass);

        physics_components[i].velocity = vector_scale(
            vector_add(vector_scale(physics_components[i].velocity, m_i),
                       vector_scale(physics_components[j].velocity, m_j)),
            1.0f / total_mass);

        physics_components[i].mass = total_mass;

        destroy_entity(j);
    }
}
```

**Step 5: Extract `update_physics_component`**

Extract lines 364-370 of `nbody_step`:

```c
static void update_physics_component(int entity_id) {
    physics_components[entity_id].velocity = vector_add(
        physics_components[entity_id].velocity,
        vector_scale(physics_components[entity_id].acceleration, dt));

    position_components[entity_id].coordinates = vector_add(
        position_components[entity_id].coordinates,
        vector_scale(physics_components[entity_id].velocity, dt));
}
```

**Step 6: Extract `check_collision_with_boundary`**

Extract lines 372-391 of `nbody_step`:

```c
static void check_collision_with_boundary(int entity_id) {
    float dist = vector_magnitude(position_components[entity_id].coordinates);
    if (dist > world_radius) {
        vector normal = vector_scale(position_components[entity_id].coordinates,
                                     1.0f / dist);
        position_components[entity_id].coordinates = vector_scale(normal,
                                                                  world_radius);
        float vn = vector_dot(physics_components[entity_id].velocity, normal);
        if (vn > 0) {
            physics_components[entity_id].velocity = vector_sub(
                physics_components[entity_id].velocity,
                vector_scale(normal, 2.0f * vn));
            physics_components[entity_id].velocity = vector_scale(
                physics_components[entity_id].velocity, 0.5f);
        }
    }
}
```

**Step 7: Rewrite `nbody_step` using extracted functions**

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

**Step 8: Build and verify**

Run: `autoreconf -i && ./configure && make clean && make`

Expected: Compiles cleanly.

**Step 9: Run and verify physics still works**

Run: `./apps/nbody/nbody -n 50 -b`

Test: Bodies attract, merge, and bounce off boundary. Behavior should be identical to before.

**Step 10: Commit**

```bash
git add apps/nbody/nbody.c
git commit -m "refactor(nbody): extract physics functions from nbody_step"
```

---

### Task 6: Extract render helper functions from `nbody_render`

**Files:**
- Modify: `apps/nbody/nbody.c` (extract render helpers)

**Step 1: Extract `entity_to_sphere`**

```c
static rt_sphere entity_to_sphere(int entity_id) {
    float mass = physics_components[entity_id].mass;
    float t = logf(mass) / logf(1000.0f);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    rt_sphere sp;
    sp.center = position_components[entity_id].coordinates;
    sp.radius = 2.0f + logf(mass) * 2.0f;
    if (sp.radius < 1.0f) sp.radius = 1.0f;
    sp.r = (uint8_t)(50 + t * 205);
    sp.g = (uint8_t)(50 * (1 - t * t));
    sp.b = (uint8_t)(255 * (1 - t * t));

    return sp;
}
```

**Step 2: Extract `ensure_render_resources`**

```c
static void ensure_render_resources(int w, int h) {
    if (!rt_scene_ptr) {
        rt_scene_ptr = rt_scene_create(MAX_ENTITIES);
    }
    if (!pixel_buffer || rt_width != w || rt_height != h) {
        free(pixel_buffer);
        pixel_buffer = calloc((size_t)w * h, sizeof(uint32_t));
        if (rt_texture) render_destroy_texture(rt_texture);
        rt_texture = render_create_texture(w, h);
        rt_width = w;
        rt_height = h;
    }
}
```

**Step 3: Extract `render_scene`**

```c
static void render_scene(const rt_camera *cam, const rt_viewport *vp) {
    int num_chunks = num_threads;
    render_chunk_args chunk_args[MAX_THREADS];
    int rows_per_chunk = vp->height / num_chunks;

    for (int c = 0; c < num_chunks; c++) {
        chunk_args[c].pixel_buf = pixel_buffer;
        chunk_args[c].viewport = vp;
        chunk_args[c].y_start = c * rows_per_chunk;
        chunk_args[c].y_end = (c == num_chunks - 1) ? vp->height : (c + 1) * rows_per_chunk;
        chunk_args[c].camera = cam;
        chunk_args[c].scene = rt_scene_ptr;
        thread_pool_submit(pool, render_chunk_task, &chunk_args[c]);
    }
    thread_pool_wait(pool);

    render_clear();
    render_texture_update(rt_texture, pixel_buffer, vp->width * (int)sizeof(uint32_t));
    render_present();
}
```

**Step 4: Rewrite `nbody_render` using extracted functions**

```c
void nbody_render(int screen_width, int screen_height) {
    int w = screen_width / RT_SCALE;
    int h = screen_height / RT_SCALE;

    ensure_render_resources(w, h);

    rt_viewport viewport = { .width = w, .height = h, .fov = 1.5708f };

    rt_scene_clear(rt_scene_ptr);
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if ((entity_masks[i] & (POSITION | PHYSICS)) != (POSITION | PHYSICS))
            continue;
        rt_scene_add_sphere(rt_scene_ptr, entity_to_sphere(i));
    }

    render_scene(camera, &viewport);
}
```

**Step 5: Build and verify**

Run: `autoreconf -i && ./configure && make clean && make`

Expected: Compiles cleanly.

**Step 6: Run and verify rendering still works**

Run: `./apps/nbody/nbody -n 50`

Expected: Identical visual output to before.

**Step 7: Commit**

```bash
git add apps/nbody/nbody.c
git commit -m "refactor(nbody): extract render helper functions from nbody_render"
```

---

### Task 7: Final verification

**Step 1: Full clean build**

Run: `autoreconf -i && ./configure && make clean && make`

Expected: Zero warnings, zero errors.

**Step 2: Run with various flags**

Run: `./apps/nbody/nbody -n 200 -b -T 4`

Test all controls: arrows, +/-, F/S, R, ESC.

**Step 3: Commit design doc**

```bash
git add docs/plans/2026-03-05-encapsulation-design.md docs/plans/2026-03-05-encapsulation.md
git commit -m "docs: add encapsulation refactoring design and implementation plan"
```
