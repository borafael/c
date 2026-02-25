# N-Body Camera Pan Design

## Overview

Add arrow key camera panning to the nbody simulation, following the same event-flag pattern used by zoom and speed controls.

## Changes

### input.h — New event flags

Add 4 flags to `input_events`: `pan_up`, `pan_down`, `pan_left`, `pan_right`.

### input.c — Key mapping

Map SDL arrow keys to the new flags:
- `SDLK_UP` → `pan_up`
- `SDLK_DOWN` → `pan_down`
- `SDLK_LEFT` → `pan_left`
- `SDLK_RIGHT` → `pan_right`

### nbody.h / nbody.c — Camera offset state and pan functions

- New state: `static float camera_offset_x = 0.0f`, `camera_offset_y = 0.0f`
- 4 new functions: `nbody_pan_up()`, `nbody_pan_down()`, `nbody_pan_left()`, `nbody_pan_right()`
- Pan speed: base amount (`10.0f` world units) divided by `zoom` (scales inversely with zoom level)
- Reset camera offset to `(0, 0)` when R is pressed (in existing reset function)

### nbody.c rendering — Apply camera offset

Modify `camera_x` / `camera_y` calculation:

```c
float camera_x = (WORLD_WIDTH / 2.0f - camera_offset_x) - (WORLD_WIDTH / 2.0f) / zoom;
float camera_y = (WORLD_HEIGHT / 2.0f - camera_offset_y) - (WORLD_HEIGHT / 2.0f) / zoom;
```

### main.c — Wire input to pan functions

Same pattern as zoom: check each flag, call corresponding function.

## Key Decisions

- **Step-based panning**: Each key press pans a fixed step, consistent with how zoom works (single-frame events, not key-held state).
- **Zoom-scaled speed**: Pan distance is divided by zoom level so movement is finer when zoomed in and coarser when zoomed out.
- **Reset on R**: Camera offset resets to `(0, 0)` along with entities.
