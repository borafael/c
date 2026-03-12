# Battleforge Mouse Interaction Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add mouse-based unit selection and move commands to the battleforge engine via `bf_pick` and `BF_CMD_SELECT`.

**Architecture:** The engine converts screen coordinates to world rays using its own camera and viewport, tests against entities and ground, and returns pick results. The shell handles mouse events and translates pick results into commands. Selection state lives in the engine.

**Tech Stack:** C, GNU Autotools, libs/raytrace, libs/battleforge, SDL2 (shell only)

**Spec:** `docs/superpowers/specs/2026-03-12-battleforge-mouse-interaction-design.md`

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `libs/raytrace/raytrace.h` | Add `rt_camera_get_basis` and `rt_pick_sprite` declarations |
| Modify | `libs/raytrace/raytrace.c` | Implement `rt_camera_get_basis` and `rt_pick_sprite` |
| Modify | `libs/battleforge/battleforge.h` | Add pick types, pick result, `bf_pick`, `BF_CMD_SELECT` |
| Modify | `libs/battleforge/battleforge.c` | Implement `bf_pick`, `cmd_select`, selection clearing on destroy |
| Modify | `apps/battleforge/main.c` | Add mouse event handling, remove auto-patrol |

---

## Chunk 1: Raytracer Public Helpers

### Task 1: Add rt_camera_get_basis

Expose the camera's internal orientation vectors so `bf_pick` can construct pick rays.

**Files:**
- Modify: `libs/raytrace/raytrace.h:80-85`
- Modify: `libs/raytrace/raytrace.c:180-183`

- [ ] **Step 1: Add declaration to raytrace.h**

In `libs/raytrace/raytrace.h`, after the `rt_camera_destroy` declaration (line 85), add:

```c
/**
 * Extract the camera's position and orientation basis vectors.
 */
void rt_camera_get_basis(const rt_camera *cam,
                         vector *origin, vector *forward,
                         vector *right, vector *up);
```

- [ ] **Step 2: Implement in raytrace.c**

In `libs/raytrace/raytrace.c`, after `rt_camera_destroy` (line 183), add:

```c
void rt_camera_get_basis(const rt_camera *cam,
                         vector *origin, vector *forward,
                         vector *right, vector *up) {
    *origin  = cam->origin;
    *forward = cam->forward;
    *right   = cam->right;
    *up      = cam->up;
}
```

- [ ] **Step 3: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors or warnings.

- [ ] **Step 4: Commit**

```bash
git add libs/raytrace/raytrace.h libs/raytrace/raytrace.c
git commit -m "feat(raytrace): expose camera basis via rt_camera_get_basis"
```

---

### Task 2: Add rt_pick_sprite

A combined public function that tests a ray against a billboard sprite including alpha transparency check. Wraps the existing static `intersect_sprite`, `sprite_select_frame`, and `sprite_sample` functions.

**Files:**
- Modify: `libs/raytrace/raytrace.h:85-90`
- Modify: `libs/raytrace/raytrace.c:432`

- [ ] **Step 1: Add declaration to raytrace.h**

In `libs/raytrace/raytrace.h`, after the `rt_camera_get_basis` declaration just added, add:

```c
/**
 * Test ray against a billboard sprite with alpha transparency.
 * Returns t > 0 on opaque hit, -1 on miss or transparent.
 * hit_point is set to the world-space hit position on success.
 */
float rt_pick_sprite(vector ray_origin, vector ray_dir,
                     const rt_sprite *sprite, vector camera_origin,
                     vector *hit_point);
```

- [ ] **Step 2: Implement in raytrace.c**

In `libs/raytrace/raytrace.c`, after `sprite_sample` (after line 432), add:

```c
float rt_pick_sprite(vector ray_origin, vector ray_dir,
                     const rt_sprite *sprite, vector camera_origin,
                     vector *hit_point) {
    vector spr_right, spr_up, spr_normal;
    float t = intersect_sprite(ray_origin, ray_dir, sprite, camera_origin,
                               &spr_right, &spr_up, &spr_normal);
    if (t < 0.0f) return -1.0f;

    vector hp = vector_add(ray_origin, vector_scale(ray_dir, t));
    int frame_idx = sprite_select_frame(sprite, camera_origin);
    const rt_frame *frame = &sprite->frames[frame_idx];
    uint32_t pixel = sprite_sample(sprite, frame, hp, spr_right, spr_up);
    uint8_t alpha = (pixel >> 24) & 0xFF;
    if (alpha == 0) return -1.0f;

    if (hit_point) *hit_point = hp;
    return t;
}
```

- [ ] **Step 3: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors or warnings.

- [ ] **Step 4: Commit**

```bash
git add libs/raytrace/raytrace.h libs/raytrace/raytrace.c
git commit -m "feat(raytrace): add rt_pick_sprite for ray-sprite picking with alpha"
```

---

## Chunk 2: Engine Pick and Selection

### Task 3: Add BF_CMD_SELECT and selection state

Add the select command to the public header and implement the handler in the engine. Also update `cmd_entity_destroy` to clear selection.

**Files:**
- Modify: `libs/battleforge/battleforge.h:39-61`
- Modify: `libs/battleforge/battleforge.c:27,168-171,186-199`

- [ ] **Step 1: Update battleforge.h — add pick types, BF_CMD_SELECT, and bf_pick**

In `libs/battleforge/battleforge.h`, add the pick types before the `/* --- Engine --- */` section (before line 63):

```c
/* --- Picking --- */

typedef enum {
    BF_PICK_SKY,       /* ray hit nothing */
    BF_PICK_GROUND,    /* ray hit the ground plane */
    BF_PICK_ENTITY     /* ray hit an entity sprite */
} bf_pick_type;

typedef struct {
    bf_pick_type type;
    int entity_id;       /* entity ID when type == BF_PICK_ENTITY; 0 otherwise */
    vector position;     /* world-space hit point for GROUND and ENTITY; zeroed for SKY */
} bf_pick_result;
```

Update the `bf_cmd_type` enum to add `BF_CMD_SELECT` before `BF_CMD_COUNT`:

```c
typedef enum {
    BF_CMD_CAMERA_SET,
    BF_CMD_CAMERA_MOVE,
    BF_CMD_ENTITY_CREATE,
    BF_CMD_ENTITY_DESTROY,
    BF_CMD_ENTITY_MOVE,
    BF_CMD_ENTITY_FACE,
    BF_CMD_ENTITY_SET_SPEED,
    BF_CMD_SELECT,
    BF_CMD_COUNT
} bf_cmd_type;
```

Add `select` member to the `bf_cmd` union:

```c
        struct { int id; float speed; } entity_set_speed;
        struct { int id; } select;
```

Add `bf_pick` declaration after `bf_render`:

```c
bf_pick_result bf_pick(bf_engine *e, int screen_x, int screen_y);
```

- [ ] **Step 2: Add selected_entity_id to engine struct in battleforge.c**

In `libs/battleforge/battleforge.c`, in the `struct bf_engine` definition, after `int entity_count;` (line 27), add:

```c
    int selected_entity_id;
```

- [ ] **Step 3: Implement cmd_select handler**

In `libs/battleforge/battleforge.c`, after `cmd_entity_set_speed` (after line 186), add:

```c
static void cmd_select(bf_engine *e, const bf_cmd *cmd) {
    if (cmd->select.id <= 0) {
        e->selected_entity_id = 0;
        return;
    }
    bf_entity *ent = find_entity(e, cmd->select.id);
    if (ent) e->selected_entity_id = cmd->select.id;
}
```

- [ ] **Step 4: Update cmd_entity_destroy to clear selection**

Replace the `cmd_entity_destroy` function:

```c
static void cmd_entity_destroy(bf_engine *e, const bf_cmd *cmd) {
    bf_entity *ent = find_entity(e, cmd->entity_destroy.id);
    if (ent) {
        ent->active = 0;
        if (e->selected_entity_id == ent->id)
            e->selected_entity_id = 0;
    }
}
```

- [ ] **Step 5: Add cmd_select to dispatch table**

Update the `cmd_handlers` dispatch table to include the new handler:

```c
static void (*cmd_handlers[BF_CMD_COUNT])(bf_engine *, const bf_cmd *) = {
    [BF_CMD_CAMERA_SET]        = cmd_camera_set,
    [BF_CMD_CAMERA_MOVE]       = cmd_camera_move,
    [BF_CMD_ENTITY_CREATE]     = cmd_entity_create,
    [BF_CMD_ENTITY_DESTROY]    = cmd_entity_destroy,
    [BF_CMD_ENTITY_MOVE]       = cmd_entity_move,
    [BF_CMD_ENTITY_FACE]       = cmd_entity_face,
    [BF_CMD_ENTITY_SET_SPEED]  = cmd_entity_set_speed,
    [BF_CMD_SELECT]            = cmd_select,
};
```

- [ ] **Step 6: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors or warnings.

- [ ] **Step 7: Commit**

```bash
git add libs/battleforge/battleforge.h libs/battleforge/battleforge.c
git commit -m "feat(battleforge): add BF_CMD_SELECT and selection state"
```

---

### Task 4: Implement bf_pick

The core picking function: screen coords to world ray, test entities then ground.

**Files:**
- Modify: `libs/battleforge/battleforge.c` (after `bf_render`)

- [ ] **Step 1: Implement bf_pick**

In `libs/battleforge/battleforge.c`, after the `bf_render` function, add:

```c
bf_pick_result bf_pick(bf_engine *e, int screen_x, int screen_y) {
    bf_pick_result result = { .type = BF_PICK_SKY, .entity_id = 0,
                              .position = {0.0f, 0.0f, 0.0f} };

    /* Bounds check */
    if (screen_x < 0 || screen_x >= e->config.render_width ||
        screen_y < 0 || screen_y >= e->config.render_height)
        return result;

    /* Construct ray matching the raytracer's projection */
    vector origin, forward, right, up;
    rt_camera_get_basis(e->rt_cam, &origin, &forward, &right, &up);

    float half_w = (float)e->config.render_width * 0.5f;
    float half_h = (float)e->config.render_height * 0.5f;
    float fov_factor = (float)e->config.render_height /
                       (2.0f * tanf(e->config.fov / 2.0f));

    float sx = ((float)screen_x - half_w) / fov_factor;
    float sy = -((float)screen_y - half_h) / fov_factor;

    vector ray_dir = vector_add(
        vector_add(forward, vector_scale(right, sx)),
        vector_scale(up, sy));
    ray_dir = vector_normalize(ray_dir);

    /* Test entities (closest wins) */
    float closest_t = FLT_MAX;
    int closest_id = 0;
    vector closest_pos = {0};

    for (int i = 0; i < e->entity_count; i++) {
        bf_entity *ent = &e->entities[i];
        if (!ent->active) continue;
        if (ent->sprite_id < 0 || ent->sprite_id >= e->sprite_count) continue;

        bf_sprite_def *def = &e->sprites[ent->sprite_id];
        rt_sprite spr = {
            .position = ent->position,
            .direction = ent->direction,
            .width = def->width,
            .height = def->height,
            .frame_count = def->frame_count,
            .frames = def->frames
        };

        vector hp;
        float t = rt_pick_sprite(origin, ray_dir, &spr, origin, &hp);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            closest_id = ent->id;
            closest_pos = hp;
        }
    }

    if (closest_id > 0) {
        result.type = BF_PICK_ENTITY;
        result.entity_id = closest_id;
        result.position = closest_pos;
        return result;
    }

    /* Test ground plane (y=0) */
    if (e->map_set && fabsf(ray_dir.y) > 1e-6f) {
        float t = -origin.y / ray_dir.y;
        if (t > 0.0f) {
            result.type = BF_PICK_GROUND;
            result.position = vector_add(origin, vector_scale(ray_dir, t));
            return result;
        }
    }

    return result;
}
```

- [ ] **Step 2: Add required include**

At the top of `libs/battleforge/battleforge.c`, after the existing includes, ensure `<float.h>` is included (for `FLT_MAX`):

```c
#include <float.h>
```

- [ ] **Step 3: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors or warnings.

- [ ] **Step 4: Commit**

```bash
git add libs/battleforge/battleforge.c
git commit -m "feat(battleforge): implement bf_pick for screen-to-world picking"
```

---

## Chunk 3: Shell Integration

### Task 5: Update the SDL shell with mouse interaction

Add mouse event handling, track selected entity, remove auto-patrol.

**Files:**
- Modify: `apps/battleforge/main.c`

- [ ] **Step 1: Add selected_id tracking variable**

In `apps/battleforge/main.c`, after `int patrol_dir = 1;` (line 228), replace the patrol-related variables with:

```c
    int selected_id = 0;
```

Remove these lines entirely:
- `int patrol_dir = 1;  /* 1 = going right, 0 = going left */` (line 228)

- [ ] **Step 2: Add mouse event handling to the event loop**

In the `while (SDL_PollEvent(&e))` loop (inside the main while loop), after the ESC key check (line 246), add:

```c
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                bf_pick_result pick = bf_pick(engine, e.button.x, e.button.y);
                if (e.button.button == SDL_BUTTON_LEFT) {
                    if (pick.type == BF_PICK_ENTITY) {
                        selected_id = pick.entity_id;
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_SELECT,
                            .select = { .id = pick.entity_id }
                        });
                        fprintf(stderr, "Selected entity %d\n", pick.entity_id);
                    } else {
                        selected_id = 0;
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_SELECT,
                            .select = { .id = 0 }
                        });
                        fprintf(stderr, "Deselected\n");
                    }
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    if (selected_id > 0 && pick.type == BF_PICK_GROUND) {
                        bf_command(engine, (bf_cmd){
                            .type = BF_CMD_ENTITY_MOVE,
                            .entity_move = { .id = selected_id,
                                             .position = pick.position }
                        });
                        fprintf(stderr, "Move entity %d to (%.1f, %.1f, %.1f)\n",
                                selected_id, pick.position.x,
                                pick.position.y, pick.position.z);
                    }
                }
            }
```

- [ ] **Step 3: Remove auto-patrol logic**

Remove the entire patrol block from the main loop (the `static float patrol_timer` section and `patrol_dir` toggling). This is approximately lines 273-293 in the current file:

```c
        /* Simple patrol: entity 1 bounces between two points */
        static float patrol_timer = 0.0f;
        patrol_timer += dt;
        if (patrol_timer > 3.0f) {
            patrol_timer = 0.0f;
            if (patrol_dir) {
                bf_command(engine, (bf_cmd){
                    .type = BF_CMD_ENTITY_MOVE,
                    .entity_move = { .id = 1, .position = {-8.0f, 1.0f, 0.0f} }
                });
            } else {
                bf_command(engine, (bf_cmd){
                    .type = BF_CMD_ENTITY_MOVE,
                    .entity_move = { .id = 1, .position = {8.0f, 1.0f, 0.0f} }
                });
            }
            patrol_dir = !patrol_dir;
        }
```

Also remove the initial patrol move command (line 221-224):

```c
    /* Entity 1 patrol target */
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_ENTITY_MOVE,
        .entity_move = { .id = 1, .position = {8.0f, 1.0f, 0.0f} }
    });
```

- [ ] **Step 4: Give all entities some initial speed**

Update entity 2 and entity 3 creation to have a non-zero speed so they can move when clicked:

Entity 2: change `.speed = 0.0f` to `.speed = 3.0f`
Entity 3: change `.speed = 0.0f` to `.speed = 3.0f`

- [ ] **Step 5: Verify it compiles**

Run: `cd /home/rafa/repos/c && make`
Expected: Compiles with no errors or warnings.

- [ ] **Step 6: Run and verify visually**

Run: `cd /home/rafa/repos/c && ./apps/battleforge/battleforge`
Expected:
- Window opens with 3 smiley sprites on green ground
- Left-click on a sprite: stderr prints "Selected entity N"
- Left-click on ground/sky: stderr prints "Deselected"
- Right-click on ground while entity is selected: entity walks toward clicked position, stderr prints coordinates
- WASD/arrow keys still control the camera
- FPS counter still works

- [ ] **Step 7: Commit**

```bash
git add apps/battleforge/main.c
git commit -m "feat(battleforge): add mouse selection and move commands to shell"
```

---

### Task 6: Final verification

- [ ] **Step 1: Clean rebuild**

Run: `cd /home/rafa/repos/c && make clean && make`
Expected: Full clean build with no errors or warnings.

- [ ] **Step 2: Verify both apps still work**

Run rtdemo: `./apps/rtdemo/rtdemo` — should work as before (no regressions from raytrace.h changes).
Run battleforge: `./apps/battleforge/battleforge` — mouse interaction works.

- [ ] **Step 3: Commit any remaining changes**

If any fixes were needed, commit them now.

---

## Verification Checklist

After all tasks are complete:

- [ ] `make clean && make` compiles with no errors or warnings
- [ ] `apps/battleforge/battleforge` runs and shows the demo scene
- [ ] Left-click on entity selects it (stderr confirmation)
- [ ] Left-click on ground/sky deselects (stderr confirmation)
- [ ] Right-click on ground moves selected entity to that position
- [ ] Right-click with nothing selected does nothing
- [ ] Entity walks to clicked position and stops
- [ ] Camera controls (WASD, arrows, Space, Shift) still work
- [ ] `apps/rtdemo/rtdemo` still works (no regressions)
