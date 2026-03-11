# Raytrace Sprites Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add angle-dependent billboard sprite rendering to `libs/raytrace`, with a demo sprite in the rtdemo app.

**Architecture:** Sprites are a new shape type in the raytracer. Each sprite is a camera-facing quad with multiple texture frames (one per viewing angle). Ray-quad intersection determines if a ray hits the sprite, angle selection picks the right frame, UV mapping samples the texture, and alpha=0 pixels let the ray pass through. The game layer controls animation externally by swapping frames each tick.

**Tech Stack:** C, existing raytrace library, SDL2 (demo only)

**Spec:** `docs/superpowers/specs/2026-03-11-raytrace-sprites-design.md`

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `libs/raytrace/raytrace.h` | Add `rt_frame`, `rt_sprite` types and `rt_scene_add_sprite` declaration |
| Modify | `libs/raytrace/raytrace.c` | Sprite storage, billboard intersection, angle selection, UV sampling, alpha handling |
| Modify | `apps/rtdemo/main.c` | Embedded 8-frame test sprite to demonstrate angle selection |

No new files needed.

---

## Task 1: Add sprite types to the public header

**Files:**
- Modify: `libs/raytrace/raytrace.h:7-9` (after `rt_color`, before `rt_sphere`)

- [ ] **Step 1: Add `rt_frame` and `rt_sprite` typedefs**

In `libs/raytrace/raytrace.h`, after the `rt_color` typedef (line 9) and before the `rt_sphere` typedef (line 11), add:

```c
typedef struct {
    uint32_t *pixels;   /* ARGB8888 pixel data (not owned by raytracer) */
    int width;
    int height;
} rt_frame;

typedef struct {
    vector position;     /* center in world space */
    vector direction;    /* facing direction (for angle selection only) */
    float width;         /* world-space quad width */
    float height;        /* world-space quad height */
    int frame_count;     /* number of viewing angles */
    rt_frame *frames;    /* one frame per angle, clockwise from front */
} rt_sprite;
```

- [ ] **Step 2: Add `rt_scene_add_sprite` declaration**

In `libs/raytrace/raytrace.h`, after `int rt_scene_add_box(...)` (line 91), add:

```c
int rt_scene_add_sprite(rt_scene *scene, rt_sprite sprite);
```

- [ ] **Step 3: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors (function declared but not yet defined — the linker won't complain since nothing calls it yet).

- [ ] **Step 4: Commit**

```bash
git add libs/raytrace/raytrace.h
git commit -m "feat(raytrace): add rt_frame and rt_sprite types to public header"
```

---

## Task 2: Add sprite storage to the scene

**Files:**
- Modify: `libs/raytrace/raytrace.c:16-39` (scene struct)
- Modify: `libs/raytrace/raytrace.c:41-68` (scene create)
- Modify: `libs/raytrace/raytrace.c:70-77` (scene clear)
- Modify: `libs/raytrace/raytrace.c:126-136` (scene destroy)

- [ ] **Step 1: Add sprite fields to `rt_scene` struct**

In `libs/raytrace/raytrace.c`, inside `struct rt_scene` (after the `float ambient;` field at line 38), add:

```c
    rt_sprite *sprites;
    int sprite_count;
    int sprite_capacity;
```

- [ ] **Step 2: Initialize sprite array in `rt_scene_create`**

In `rt_scene_create()`, after `s->light_capacity = DEFAULT_CAPACITY;` (line 51), add:

```c
    s->sprite_capacity   = DEFAULT_CAPACITY;
```

After `s->lights = malloc(...)` (line 60), add:

```c
    s->sprites   = malloc(sizeof(rt_sprite)  * s->sprite_capacity);
```

In the NULL check (line 62-64), add `!s->sprites` to the condition:

```c
    if (!s->spheres || !s->planes || !s->discs ||
        !s->cylinders || !s->triangles || !s->boxes || !s->lights ||
        !s->sprites) {
```

- [ ] **Step 3: Reset sprite count in `rt_scene_clear`**

In `rt_scene_clear()`, after `scene->box_count = 0;` (line 76), add:

```c
    scene->sprite_count   = 0;
```

- [ ] **Step 4: Free sprites in `rt_scene_destroy`**

In `rt_scene_destroy()`, after `free(scene->lights);` (line 134), add:

```c
    free(scene->sprites);
```

- [ ] **Step 5: Implement `rt_scene_add_sprite`**

After the existing `rt_scene_add_light` function (line 124), add:

```c
int rt_scene_add_sprite(rt_scene *scene, rt_sprite sprite) {
    if (scene->sprite_count >= scene->sprite_capacity) return -1;
    scene->sprites[scene->sprite_count++] = sprite;
    return 0;
}
```

- [ ] **Step 6: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors.

- [ ] **Step 7: Commit**

```bash
git add libs/raytrace/raytrace.c
git commit -m "feat(raytrace): add sprite storage to scene (create, clear, destroy, add)"
```

---

## Task 3: Implement sprite intersection and rendering

This is the core rendering task. It adds the billboard quad intersection, angle selection, UV sampling, alpha transparency, and lighting to the render loop.

**Files:**
- Modify: `libs/raytrace/raytrace.c:340-454` (render_chunk function area)

- [ ] **Step 1: Add the `intersect_sprite` static function**

Before `rt_render_chunk` (around line 340), add this static function. It returns the ray distance `t` or -1 on miss. It also outputs the billboard's right/up axes and the quad normal via pointer parameters (needed for UV mapping and lighting later):

```c
static float intersect_sprite(vector ro, vector rd,
                               const rt_sprite *spr, vector cam_origin,
                               vector *out_right, vector *out_up,
                               vector *out_normal) {
    /* Billboard normal: from sprite toward camera */
    vector to_cam = vector_sub(cam_origin, spr->position);
    vector normal = vector_normalize(to_cam);

    /* Ray-plane intersection */
    float denom = vector_dot(rd, normal);
    if (fabsf(denom) < 1e-6f) return -1.0f;
    float t = vector_dot(vector_sub(spr->position, ro), normal) / denom;
    if (t < 0.0f) return -1.0f;

    /* Billboard axes */
    vector world_up = {0.0f, 1.0f, 0.0f};
    vector right = vector_normalize(vector_cross(normal, world_up));
    if (vector_magnitude(right) < 0.001f) {
        vector world_fwd = {0.0f, 0.0f, 1.0f};
        right = vector_normalize(vector_cross(normal, world_fwd));
    }
    vector up = vector_cross(right, normal);

    /* Check if hit is within quad bounds */
    vector hp = vector_add(ro, vector_scale(rd, t));
    vector diff = vector_sub(hp, spr->position);
    float local_x = vector_dot(diff, right);
    float local_y = vector_dot(diff, up);

    float half_w = spr->width * 0.5f;
    float half_h = spr->height * 0.5f;
    if (local_x < -half_w || local_x > half_w ||
        local_y < -half_h || local_y > half_h)
        return -1.0f;

    *out_right = right;
    *out_up = up;
    *out_normal = normal;
    return t;
}
```

- [ ] **Step 2: Add the `sprite_select_frame` static function**

After `intersect_sprite`, add the angle selection helper:

```c
static int sprite_select_frame(const rt_sprite *spr, vector cam_origin) {
    if (spr->frame_count <= 1) return 0;

    vector to_cam = vector_sub(cam_origin, spr->position);
    /* Project onto XZ plane */
    float xz_mag = sqrtf(to_cam.x * to_cam.x + to_cam.z * to_cam.z);
    if (xz_mag < 1e-4f) return 0; /* degenerate: camera directly above/below */

    float angle = atan2f(to_cam.x, to_cam.z) - atan2f(spr->direction.x, spr->direction.z);
    if (angle < 0.0f) angle += 2.0f * (float)M_PI;
    if (angle >= 2.0f * (float)M_PI) angle -= 2.0f * (float)M_PI;

    float sector = 2.0f * (float)M_PI / spr->frame_count;
    int index = (int)roundf(angle / sector) % spr->frame_count;
    return index;
}
```

- [ ] **Step 3: Add the `sprite_sample` static function**

After `sprite_select_frame`, add the UV sampling function. Returns the sampled ARGB pixel:

```c
static uint32_t sprite_sample(const rt_sprite *spr, const rt_frame *frame,
                               vector hp, vector right, vector up) {
    vector diff = vector_sub(hp, spr->position);
    float local_x = vector_dot(diff, right);
    float local_y = vector_dot(diff, up);

    float u = (local_x / spr->width) + 0.5f;
    float v = 0.5f - (local_y / spr->height);

    int px = (int)(u * frame->width);
    int py = (int)(v * frame->height);
    if (px < 0) px = 0;
    if (px >= frame->width) px = frame->width - 1;
    if (py < 0) py = 0;
    if (py >= frame->height) py = frame->height - 1;

    return frame->pixels[py * frame->width + px];
}
```

- [ ] **Step 4: Add the sprite loop to `rt_render_chunk`**

In `rt_render_chunk()`, after the `/* Boxes */` loop (line 424-434) and before the `if (hit)` block (line 436), add the sprite intersection loop:

```c
            /* Sprites */
            for (int i = 0; i < scene->sprite_count; i++) {
                vector spr_right, spr_up, spr_normal;
                float t = intersect_sprite(camera->origin, dir,
                                           &scene->sprites[i], camera->origin,
                                           &spr_right, &spr_up, &spr_normal);
                if (t > 0.0f && t < closest_t) {
                    int frame_idx = sprite_select_frame(&scene->sprites[i],
                                                        camera->origin);
                    const rt_frame *frame = &scene->sprites[i].frames[frame_idx];
                    vector hp = vector_add(camera->origin, vector_scale(dir, t));
                    uint32_t pixel = sprite_sample(&scene->sprites[i], frame,
                                                    hp, spr_right, spr_up);
                    uint8_t alpha = (pixel >> 24) & 0xFF;
                    if (alpha == 0) continue; /* transparent — skip */

                    closest_t = t;
                    normal = spr_normal;
                    /* Extract RGB from ARGB pixel into rt_color */
                    color.r = (pixel >> 16) & 0xFF;
                    color.g = (pixel >>  8) & 0xFF;
                    color.b =  pixel        & 0xFF;
                    hit = 1;
                }
            }
```

- [ ] **Step 5: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors or warnings.

- [ ] **Step 6: Commit**

```bash
git add libs/raytrace/raytrace.c
git commit -m "feat(raytrace): implement sprite billboard intersection, angle selection, and texture sampling"
```

---

## Task 4: Add a demo sprite to rtdemo

**Files:**
- Modify: `apps/rtdemo/main.c`

- [ ] **Step 1: Create embedded sprite frame data**

In `apps/rtdemo/main.c`, after the `#define FOV` line (line 9) and before the `build_scene` function (line 11), add the embedded sprite data. This creates 8 frames of a simple 16x16 arrow pattern — each angle shows the arrow pointing in a different direction, with a distinct tint color so the angle selection is visually obvious:

```c
#define SPRITE_SIZE 16

/* Helper: create a solid-color pixel with alpha */
#define PX(r,g,b) (0xFF000000u | ((r)<<16) | ((g)<<8) | (b))
#define TP 0x00000000u  /* transparent */

/*
 * 8 sprite frames (16x16), one per viewing angle.
 * Each is a colored arrow pointing in the sprite's "perceived" direction.
 * Frame 0 = front (facing viewer), Frame 4 = back, etc.
 * Colors: 0=red, 1=orange, 2=yellow, 3=lime, 4=green, 5=cyan, 6=blue, 7=purple
 */

static uint32_t frame_data_0[SPRITE_SIZE * SPRITE_SIZE]; /* front - red */
static uint32_t frame_data_1[SPRITE_SIZE * SPRITE_SIZE]; /* front-right - orange */
static uint32_t frame_data_2[SPRITE_SIZE * SPRITE_SIZE]; /* right - yellow */
static uint32_t frame_data_3[SPRITE_SIZE * SPRITE_SIZE]; /* back-right - lime */
static uint32_t frame_data_4[SPRITE_SIZE * SPRITE_SIZE]; /* back - green */
static uint32_t frame_data_5[SPRITE_SIZE * SPRITE_SIZE]; /* back-left - cyan */
static uint32_t frame_data_6[SPRITE_SIZE * SPRITE_SIZE]; /* left - blue */
static uint32_t frame_data_7[SPRITE_SIZE * SPRITE_SIZE]; /* front-left - purple */

static void fill_arrow_frame(uint32_t *buf, uint32_t fg) {
    /* Fill with transparency, then draw a simple diamond/arrow shape */
    for (int i = 0; i < SPRITE_SIZE * SPRITE_SIZE; i++)
        buf[i] = TP;

    /* Draw a filled diamond in the center */
    int cx = SPRITE_SIZE / 2;
    int cy = SPRITE_SIZE / 2;
    for (int y = 0; y < SPRITE_SIZE; y++) {
        for (int x = 0; x < SPRITE_SIZE; x++) {
            int dx = abs(x - cx);
            int dy = abs(y - cy);
            if (dx + dy <= SPRITE_SIZE / 3)
                buf[y * SPRITE_SIZE + x] = fg;
        }
    }

    /* Draw an upward arrow tip (top 4 rows) to indicate facing */
    for (int y = 1; y < 5; y++) {
        for (int x = cx - y; x <= cx + y; x++) {
            if (x >= 0 && x < SPRITE_SIZE)
                buf[(cy - SPRITE_SIZE/3 - 1 + y) * SPRITE_SIZE + x] = fg;
        }
    }
}

static void init_sprite_frames(void) {
    fill_arrow_frame(frame_data_0, PX(255,  60,  60));  /* red */
    fill_arrow_frame(frame_data_1, PX(255, 160,  40));  /* orange */
    fill_arrow_frame(frame_data_2, PX(255, 255,  40));  /* yellow */
    fill_arrow_frame(frame_data_3, PX(160, 255,  40));  /* lime */
    fill_arrow_frame(frame_data_4, PX( 40, 255,  40));  /* green */
    fill_arrow_frame(frame_data_5, PX( 40, 255, 255));  /* cyan */
    fill_arrow_frame(frame_data_6, PX( 40,  80, 255));  /* blue */
    fill_arrow_frame(frame_data_7, PX(200,  40, 255));  /* purple */
}
```

- [ ] **Step 2: Add sprite frames array and add sprite to scene**

In the `build_scene` function, after the `rt_scene_add_light` call (line 73) and before the camera creation (line 75), add:

```c
    static rt_frame sprite_frames[8];
    init_sprite_frames();
    sprite_frames[0] = (rt_frame){ frame_data_0, SPRITE_SIZE, SPRITE_SIZE };
    sprite_frames[1] = (rt_frame){ frame_data_1, SPRITE_SIZE, SPRITE_SIZE };
    sprite_frames[2] = (rt_frame){ frame_data_2, SPRITE_SIZE, SPRITE_SIZE };
    sprite_frames[3] = (rt_frame){ frame_data_3, SPRITE_SIZE, SPRITE_SIZE };
    sprite_frames[4] = (rt_frame){ frame_data_4, SPRITE_SIZE, SPRITE_SIZE };
    sprite_frames[5] = (rt_frame){ frame_data_5, SPRITE_SIZE, SPRITE_SIZE };
    sprite_frames[6] = (rt_frame){ frame_data_6, SPRITE_SIZE, SPRITE_SIZE };
    sprite_frames[7] = (rt_frame){ frame_data_7, SPRITE_SIZE, SPRITE_SIZE };

    rt_scene_add_sprite(*scene, (rt_sprite){
        .position = {0.0f, 1.0f, 3.0f},
        .direction = {0.0f, 0.0f, 1.0f},   /* facing +Z */
        .width = 2.0f,
        .height = 2.0f,
        .frame_count = 8,
        .frames = sprite_frames
    });
```

- [ ] **Step 3: Call `init_sprite_frames` and verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors.

- [ ] **Step 4: Run the demo and verify visually**

Run: `cd /home/rafa/repos/c && ./apps/rtdemo/rtdemo`
Expected:
- A colored diamond shape is visible floating in the scene at position (0, 1, 3)
- As the camera orbits, the diamond changes color (red → orange → yellow → lime → green → cyan → blue → purple → back to red) — proving angle selection works
- The diamond has transparent edges (background shows through the non-diamond pixels)

- [ ] **Step 5: Commit**

```bash
git add apps/rtdemo/main.c
git commit -m "feat(rtdemo): add 8-angle test sprite to demonstrate sprite rendering"
```

---

## Verification Checklist

After all tasks are complete:

- [ ] `make` compiles cleanly with no warnings
- [ ] Demo runs and shows the sprite among existing shapes
- [ ] Sprite changes color/frame as camera orbits (angle selection works)
- [ ] Transparent pixels around the sprite shape show the background (alpha works)
- [ ] Sprite is lit consistently with other objects (shading works)
- [ ] Existing shapes still render correctly (no regressions)
