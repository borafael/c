# Battleforge Engine Design Spec

## Overview

`libs/battleforge` is a headless scene engine for building Dark Omen-style real-time tactical games. It accepts commands, advances a simulation, and renders to a pixel buffer. No display dependencies — shells (SDL, Electron/WASM, etc.) handle display and input translation.

The engine manages a persistent 3D world with a camera, terrain, and sprite-based entities. It knows nothing about game rules (regiments, combat, morale). Game logic lives in a separate layer above the engine that issues commands.

## Architecture

```
Shell (SDL, Electron, etc.)
  |  translates input to commands
  v
libs/battleforge (scene engine)
  |  uses internally
  v
libs/raytrace (rendering) + libs/thread (multithreading)
```

Data flows one direction: commands in, pixels out.

```
Shell -> bf_command() -> command queue -> bf_tick() -> bf_render() -> pixels -> Shell
```

## Public API

### Lifecycle

```c
bf_engine  *bf_create(bf_config config);
void        bf_destroy(bf_engine *e);
```

### Configuration

```c
typedef struct {
    int render_width;
    int render_height;
    float fov;            /* field of view in radians */
    int num_threads;      /* 0 = auto-detect */
} bf_config;
```

### Sprite Registration

Sprite definitions are registered once and referenced by id. Many entities can share one sprite definition.

```c
typedef struct {
    float width;          /* world-space quad width */
    float height;         /* world-space quad height */
    int frame_count;      /* number of viewing angles */
    rt_frame *frames;     /* one frame per angle, clockwise from front */
} bf_sprite_def;

int bf_register_sprite(bf_engine *e, bf_sprite_def def);
```

Returns a `sprite_id` (>= 0) on success, -1 on failure.

The engine does not own the frame pixel data. The caller is responsible for its lifetime (same ownership model as `libs/raytrace`).

### Map

```c
typedef struct {
    float width;          /* world units */
    float depth;          /* world units */
    uint8_t r, g, b;     /* ground color */
    float ambient;        /* ambient light level (0-1) */
    vector light_dir;     /* directional light direction */
    float light_intensity;/* directional light intensity (0-1) */
} bf_map;

void bf_set_map(bf_engine *e, bf_map map);
```

Starts as a flat colored ground plane at y=0. Future evolution path: heightmap grid, terrain textures, static objects (trees, buildings), water, terrain types affecting gameplay.

### Commands

```c
typedef enum {
    BF_CMD_CAMERA_SET,
    BF_CMD_CAMERA_MOVE,
    BF_CMD_ENTITY_CREATE,
    BF_CMD_ENTITY_DESTROY,
    BF_CMD_ENTITY_MOVE,
    BF_CMD_ENTITY_FACE,
    BF_CMD_ENTITY_SET_SPEED,
    BF_CMD_COUNT            /* sentinel for table size */
} bf_cmd_type;

typedef struct {
    bf_cmd_type type;
    union {
        struct { vector position; vector direction; } camera_set;
        struct { vector delta; } camera_move;
        struct { int id; int sprite_id; vector position; vector direction; float speed; } entity_create;
        struct { int id; } entity_destroy;
        struct { int id; vector position; } entity_move;
        struct { int id; vector direction; } entity_face;
        struct { int id; float speed; } entity_set_speed;
    };
} bf_cmd;

int bf_command(bf_engine *e, bf_cmd cmd);
```

Returns 0 on success, -1 if the command queue is full. Commands are queued and processed on the next `bf_tick`. They are plain data structs — serializable for future replays and networking.

Commands are dispatched via a function pointer table indexed by `bf_cmd_type` (no switch statements).

### Simulation and Rendering

```c
void bf_tick(bf_engine *e, float dt);
void bf_render(bf_engine *e, uint32_t *pixel_buf);
```

`bf_tick` processes the command queue and advances entity movement. `BF_CMD_ENTITY_MOVE` sets the entity's target position — the entity then moves toward it smoothly at its speed over subsequent ticks (`speed * dt` per tick). When the entity reaches the target, it stops. `BF_CMD_ENTITY_SET_SPEED` changes an entity's movement speed.

`bf_render` builds a raytracer scene from the current state (camera, map plane, entity sprites with angle selection), renders it multithreaded via `libs/thread`, and writes ARGB8888 pixels to the provided buffer. The buffer must be `render_width * render_height` uint32_t's.

## Internal Design

### Entity Storage

```c
typedef struct {
    int id;
    int sprite_id;
    vector position;
    vector direction;
    vector target;
    float speed;
    int active;
} bf_entity;
```

Entities are stored in a flat array with a count/capacity pattern (same as `rt_scene` shape arrays). Entity ids are supplied by the caller in the `BF_CMD_ENTITY_CREATE` command. The caller is responsible for choosing unique ids.

### Camera

```c
typedef struct {
    vector position;
    vector direction;
} bf_camera;
```

Stored internally in the engine. Updated by camera commands. Converted to an `rt_camera` at render time.

### Command Queue

A circular buffer of `bf_cmd` structs. `bf_command()` enqueues, `bf_tick()` drains. Fixed capacity (e.g., 1024 commands). If the queue is full, `bf_command()` silently drops the command and returns -1. The function signature is `int bf_command(bf_engine *e, bf_cmd cmd)` — returns 0 on success, -1 on overflow.

### Command Dispatch

Function pointer table indexed by `bf_cmd_type`:

```c
static void (*cmd_handlers[BF_CMD_COUNT])(bf_engine *, const bf_cmd *);
```

Each handler reads only the relevant union member for its command type.

### Render Pipeline

`bf_render` does the following each frame:

1. Create/update `rt_camera` from `bf_camera`
2. Clear the raytracer scene (note: `rt_scene_clear` must also clear lights — fix in `libs/raytrace` if needed)
3. Add directional light(s) and set ambient
4. Add map ground plane
5. For each active entity: look up its `bf_sprite_def`, compose an `rt_sprite` using the entity's `position` and `direction` combined with the sprite def's `width`, `height`, `frame_count`, and `frames`, then add it to the scene
6. Render multithreaded via `rt_render_chunk` split across thread pool
7. Output pixels

The raytracer scene is rebuilt each frame. This is simple and avoids synchronization issues between game state and render state.

## Future GPU Acceleration

The `bf_render` function is the abstraction boundary. A future `bf_config.renderer` option could select between CPU (current raytracer) and GPU (OpenGL/Vulkan) paths. The shell never knows which renderer produced the pixels — same API, same pixel buffer output.

## Future Deterministic Simulation

For networked multiplayer and replays, `bf_tick` internals can be converted to integer fixed-point arithmetic (all positions as scaled integers, e.g., scale 10000). This is an internal refactor that does not change the public API. The command struct and `bf_render` output remain the same.

## Separation of Concerns

- **`libs/battleforge`** — scene management, entity movement, rendering. Knows nothing about regiments, combat, or game rules.
- **Game logic layer** (future) — regiments, combat, morale, AI. Sits above the engine, issues commands.
- **Shell** (future) — displays pixels, translates user input to engine commands. Interchangeable (SDL, Electron/WASM).

## File Structure

### New files

```
libs/battleforge/
    battleforge.h         (public API)
    battleforge.c         (implementation)
    Makefile.am           (build config)

apps/battleforge/
    main.c                (SDL shell — display + input + engine)
    Makefile.am           (build config)
```

### Modified files

```
configure.ac              (add battleforge lib and app)
Makefile.am               (add to SUBDIRS)
```

### Dependencies

`libs/battleforge` depends on:
- `libs/raytrace` (rendering)
- `libs/thread` (multithreaded render)
- `libs/math` (vector.h)

`apps/battleforge` depends on:
- `libs/battleforge`
- SDL2 (display only)

## First Deliverable

A minimal `apps/battleforge` shell that demonstrates the engine working:

- Creates a `bf_engine` with a flat green map
- Registers a test sprite (reuse the smiley face from rtdemo)
- Places a few entities on the map
- Keyboard commands move the camera (WASD for movement, arrow keys for rotation)
- One entity moves back and forth to demonstrate `bf_tick` entity movement
- Renders at interactive framerates with multithreading
- FPS counter in window title
