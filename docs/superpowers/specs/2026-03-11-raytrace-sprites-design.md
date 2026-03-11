# Raytrace Sprites Design Spec

## Overview

Add angle-dependent sprite rendering to the `libs/raytrace` library. Sprites are billboard quads that display different texture frames based on the viewing angle relative to the sprite's facing direction. This enables game entities to be rendered as 2D textured sprites within the 3D raytraced scene, without requiring full 3D models.

## Data Model

### rt_frame

A single image — pixel data with dimensions.

```c
typedef struct {
    uint32_t *pixels;   // ARGB8888 pixel data
    int width;          // texture width in pixels
    int height;         // texture height in pixels
} rt_frame;
```

### rt_sprite

A positioned, oriented object with angle-dependent texture frames.

```c
typedef struct {
    vector position;     // center in world space
    vector direction;    // facing direction (used for angle selection only)
    float width;         // world-space width of the billboard quad
    float height;        // world-space height of the billboard quad
    int frame_count;     // number of viewing angles (1, 8, 16, etc.)
    rt_frame *frames;    // one frame per angle, ordered clockwise from "front"
} rt_sprite;
```

### Scene integration

The `rt_scene` struct gains a new sprite array, following the same pattern as existing shape arrays:

```c
rt_sprite *sprites;
int sprite_count;
int sprite_capacity;
```

A new public function is added:

```c
int rt_scene_add_sprite(rt_scene *scene, rt_sprite sprite);
```

Returns 0 on success, -1 when at capacity (matches the convention of all existing `rt_scene_add_*` functions).

## Rendering Pipeline

Sprites participate in the existing ray intersection loop alongside all other shapes. For each ray, every sprite is tested.

### 1. Billboard quad construction

For each sprite, construct a temporary quad oriented to face the camera:

- **Center**: `sprite.position`
- **Normal**: normalized vector from sprite position toward camera origin
- **Right axis**: `cross(normal, world_up)`, normalized. When the normal is nearly parallel to `world_up` (sprite directly above/below camera), fall back to `cross(normal, world_forward)` where `world_forward = (0, 0, 1)` — same approach used in the existing camera orientation code.
- **Up axis**: `cross(right, normal)`, normalized
- **Half-extents**: `sprite.width / 2` horizontally, `sprite.height / 2` vertically

### 2. Ray-quad intersection

- Compute ray-plane intersection using the quad's normal and center (same math as existing disc intersection)
- If hit, project the hit point onto the quad's local right/up axes
- Check if the projected coordinates are within half-width and half-height
- If out of bounds, no hit

### 3. Angle selection

Determine which frame to display based on viewing angle:

```
angle = atan2(to_camera.x, to_camera.z) - atan2(direction.x, direction.z)
// Normalize angle into [0, 2*PI)
if (angle < 0) angle += 2*PI;
if (angle >= 2*PI) angle -= 2*PI;
index = (int)round(angle / (2*PI / frame_count)) % frame_count
```

Where `to_camera` is the vector from the sprite to the camera projected onto the XZ plane.

**Degenerate case**: when the XZ magnitude of `to_camera` is near zero (camera directly above/below sprite), default to frame index 0. This is a known limitation — vertical viewing angles don't have meaningful horizontal facing.

This computation is cheap (two `atan2` calls) and is done per-ray per-sprite hit. Precomputing per-sprite is possible but unnecessary given the low cost.

### 4. UV mapping and texture sampling

Map the hit point to a pixel in the selected frame:

```
local_x = dot(hit_point - sprite.position, right)
local_y = dot(hit_point - sprite.position, up)
u = (local_x / width) + 0.5
v = 0.5 - (local_y / height)
px = clamp((int)(u * frame.width), 0, frame.width - 1)
py = clamp((int)(v * frame.height), 0, frame.height - 1)
pixel = frame.pixels[py * frame.width + px]
```

The `v` coordinate is flipped because pixel data has row 0 at the top, while world space has Y pointing up.

### 5. Alpha transparency

- Check the alpha channel of the sampled pixel: `(pixel >> 24) & 0xFF`
- If alpha is 0: ignore this hit, continue the ray (find next intersection)
- If alpha is non-zero: use this pixel as the hit color

### 6. Lighting

Apply the same ambient + directional lighting model used for all other shapes. The surface normal for lighting is the quad's normal (facing the camera).

To bridge the color formats: extract R, G, B from the ARGB8888 pixel (`(pixel >> 16) & 0xFF`, etc.), apply the shade factor, then recombine into ARGB8888 for the framebuffer.

## Separation of Concerns

The raytracer knows nothing about animation. It only knows:

- A sprite's current position, direction, and dimensions
- The current set of frames (one per angle)

The game layer is responsible for:

- Tracking which animation is playing (walk, attack, idle, etc.)
- Advancing the frame timer
- Updating `sprite.frames[]` each tick with the correct frame data for the current animation state

This keeps `libs/raytrace` as a pure rendering library.

## Transparency Format

ARGB8888 (already the framebuffer format). Alpha byte at bits 24-31:

- `0x00` = fully transparent (ray passes through)
- `0xFF` = fully opaque

Semi-transparency is not supported in this initial implementation.

## Memory Ownership

The `rt_frame.pixels` pointer is **not owned** by the raytracer. The caller is responsible for allocating and freeing pixel data. `rt_scene_destroy()` frees the `sprites` array itself but does not free individual frame pixel buffers. This matches the pattern where sprite pixel data may be static (embedded arrays) or managed by the game layer.

## Changes Summary

### New types in `raytrace.h`

- `rt_frame` — pixel data + dimensions
- `rt_sprite` — position, direction, width, height, frame_count, frames

### New functions in `raytrace.h`

- `rt_scene_add_sprite()` — adds a sprite to the scene

### Modified in `raytrace.c`

- `rt_scene_create()` — initialize sprite array
- `rt_scene_destroy()` — free sprite array
- `rt_scene_clear()` — reset sprite count to zero alongside other shapes
- `rt_render_chunk()` — test rays against sprites alongside existing shapes
- New internal helpers: billboard construction, UV mapping, texture sampling, alpha check

### No changes to

- Camera system (`rt_camera`)
- Existing shape types and their intersection code
- Lighting model
- Viewport

### Demo app (`apps/rtdemo/main.c`)

- Add a test sprite to the scene with embedded pixel data
- The sprite will use 8 angle frames with a simple arrow/directional pattern (different colors or shapes per angle) so the angle selection is visually obvious as the camera orbits
- Positioned among the existing shapes at a visible location
