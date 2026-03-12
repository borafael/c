# Battleforge Mouse Interaction Design

## Overview

Add mouse-based unit selection and move commands to the battleforge engine. The player left-clicks to select a unit, right-clicks the ground to move the selected unit. The engine handles screen-to-world conversion via `bf_pick`, keeping the shell free of projection math.

## Architecture

The engine already owns the camera, viewport, and scene geometry. It is the natural place to convert screen coordinates to world-space rays and test them against entities and terrain. The shell remains a thin input/display layer.

```
Left-click → shell calls bf_pick(screen_x, screen_y)
           → engine returns BF_PICK_ENTITY { id=3, position }
           → shell sends BF_CMD_SELECT { id=3 }

Right-click → shell calls bf_pick(screen_x, screen_y)
            → engine returns BF_PICK_GROUND { position=(5,0,-2) }
            → shell sends BF_CMD_ENTITY_MOVE { id=selected, position }
```

## New Types

### bf_pick_type

```c
typedef enum {
    BF_PICK_SKY,       /* ray hit nothing — clicked the sky */
    BF_PICK_GROUND,    /* ray hit the ground plane */
    BF_PICK_ENTITY     /* ray hit an entity sprite */
} bf_pick_type;
```

### bf_pick_result

```c
typedef struct {
    bf_pick_type type;
    int entity_id;       /* entity ID when type == BF_PICK_ENTITY; 0 otherwise */
    vector position;     /* world-space hit point for GROUND and ENTITY; zeroed for SKY */
} bf_pick_result;
```

For `BF_PICK_SKY`: `entity_id` is 0 and `position` is zeroed. Callers must check `type` before reading other fields.

## New API

### bf_pick

```c
bf_pick_result bf_pick(bf_engine *e, int screen_x, int screen_y);
```

Converts screen pixel (screen_x, screen_y) to a world-space ray using the engine's camera and viewport (FOV, render dimensions). Tests the ray against entities and terrain.

**Screen coordinate validation:** If screen_x or screen_y is outside the valid range `[0, render_width)` x `[0, render_height)`, returns `BF_PICK_SKY` immediately.

**Intersection priority:**

1. **Entities first** — iterates over the engine's `bf_entity` array (not the raytracer scene's sprite list), constructs a temporary `rt_sprite` for each active entity, and tests ray intersection with alpha transparency checking. If multiple entities are hit, returns the closest. Returns `BF_PICK_ENTITY` with the entity's `bf_entity.id` and the hit position.
2. **Ground plane** — if no entity was hit and a map is set (`map_set != 0`), tests against the ground plane (y=0). Returns `BF_PICK_GROUND` with the world-space position.
3. **Sky** — if nothing was hit, or if no map is set and no entity was hit, returns `BF_PICK_SKY`.

### BF_CMD_SELECT

New command added to `bf_cmd_type` enum (before `BF_CMD_COUNT`):

```c
BF_CMD_SELECT
```

Union member:

```c
struct { int id; } select;  /* id <= 0 to deselect */
```

The engine stores `selected_entity_id` in its internal state. Setting id <= 0 clears the selection.

**Entity destruction clears selection:** When `BF_CMD_ENTITY_DESTROY` is processed, if the destroyed entity is the currently selected one, `selected_entity_id` is reset to 0.

## Modified Types

### bf_cmd_type

Add `BF_CMD_SELECT` before `BF_CMD_COUNT`:

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

### bf_cmd

Add select member to the union:

```c
struct { int id; } select;
```

### bf_engine (internal)

Add to the engine struct:

```c
int selected_entity_id;  /* 0 = no selection */
```

## Shell Changes

### Event handling

In the SDL event loop, handle mouse button events:

- **Left-click (SDL_BUTTON_LEFT):**
  1. Call `bf_pick(engine, event.button.x, event.button.y)`
  2. If `BF_PICK_ENTITY`: send `BF_CMD_SELECT` with the entity ID
  3. If `BF_PICK_GROUND` or `BF_PICK_SKY`: send `BF_CMD_SELECT` with id=0 (deselect)

- **Right-click (SDL_BUTTON_RIGHT):**
  1. If no entity is selected, ignore
  2. Call `bf_pick(engine, event.button.x, event.button.y)`
  3. If `BF_PICK_GROUND`: send `BF_CMD_ENTITY_MOVE` with the selected entity ID and the hit position

### Removal of automatic patrol

The demo's automatic patrol logic (entity 1 bouncing back and forth) should be removed, since the player now controls movement via mouse.

## Ray Construction Detail

The screen-to-ray conversion in `bf_pick` must exactly match the raytracer's projection to ensure pick alignment with rendered pixels. The raytracer (`rt_render_chunk`) uses:

```c
float fov_factor = (float)height / (2.0f * tanf(fov / 2.0f));
float sx = ((float)x - half_w) / fov_factor;
float sy = -((float)y - half_h) / fov_factor;
dir = normalize(forward + right * sx + up * sy);
```

`bf_pick` must replicate this exactly:

```
fov_factor = render_height / (2.0 * tan(fov / 2.0))
sx = (screen_x - render_width / 2.0) / fov_factor
sy = -(screen_y - render_height / 2.0) / fov_factor
ray_dir = normalize(forward + right * sx + up * sy)
ray_origin = camera_position
```

### Camera basis access

The camera's `forward`, `right`, and `up` vectors are needed for ray construction but are currently hidden inside the opaque `rt_camera` struct. A new public accessor is required in `raytrace.h`:

```c
void rt_camera_get_basis(const rt_camera *cam,
                         vector *origin, vector *forward,
                         vector *right, vector *up);
```

This extracts the camera's internal orientation without breaking encapsulation — the struct remains opaque, but `bf_pick` can read the basis.

## Intersection Reuse

`bf_pick` needs to intersect a ray with entity sprites (including alpha transparency check) and the ground plane.

### Sprite intersection

The raytracer's `intersect_sprite` is a static function with signature:

```c
static float intersect_sprite(vector ro, vector rd,
                               const rt_sprite *spr, vector cam_origin,
                               vector *out_right, vector *out_up,
                               vector *out_normal);
```

The `out_right` and `out_up` outputs are required by `sprite_sample` to map the hit point to a texture pixel for the alpha transparency check. A combined public function is exposed in `raytrace.h`:

```c
/* Test ray against a billboard sprite with alpha transparency.
   Returns t > 0 on opaque hit, -1 on miss or transparent.
   hit_point is set to the world-space hit position on success. */
float rt_pick_sprite(vector ray_origin, vector ray_dir,
                     const rt_sprite *sprite, vector camera_origin,
                     vector *hit_point);
```

Internally, `rt_pick_sprite` calls `intersect_sprite`, then `sprite_select_frame`, then `sprite_sample`, and checks the alpha channel. This keeps `bf_pick` from needing to know about billboard math or texture sampling.

### Ground intersection

Simple ray-plane intersection for y=0, implemented directly in `bf_pick` (trivial math, no need for a raytracer helper):

```
t = -ray_origin.y / ray_dir.y  (if ray_dir.y != 0 and t > 0)
hit_point = ray_origin + ray_dir * t
```

### Entity-to-sprite mapping

`bf_pick` iterates over `e->entities[]`, not the raytracer scene's sprite list. For each active entity, it constructs a temporary `rt_sprite` from the entity's position, direction, and its registered `bf_sprite_def` (same construction as `bf_render`), then calls `rt_pick_sprite`. This avoids adding an entity ID field to `rt_sprite` and keeps the raytracer decoupled from game concepts.

## What This Design Does NOT Include

- Selection highlighting / visual feedback (future)
- Multi-selection / box select (future)
- Right-click on entity for attack/follow commands (future)
- Deselect on ESC (could be added trivially in shell)
