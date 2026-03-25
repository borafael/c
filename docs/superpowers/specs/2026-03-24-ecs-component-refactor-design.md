# ECS Component Refactor Design Spec

## Overview

Refactor `libs/battleforge` from a monolithic `bf_entity` struct into a component-based ECS (Entity Component System) architecture. Add a general-purpose INI parser (`libs/ini`) and data-driven unit/map definitions loaded from INI files.

This refactor restructures existing functionality without adding new game mechanics. The architecture is designed to accommodate future additions: combat, abilities, states, and scripting.

## Architecture

```
Client (SDL shell)
  |  INI parsing (libs/ini), sprite loading (libs/slice), input
  v
Engine (libs/battleforge)
  |-- ECS: component arrays, bitmasks, free list
  |-- Systems: locomotion, animation, terrain snap
  |-- Unit defs: stored by index, name for lookup/debugging
  |-- Game rules: state interactions, ability execution (future, C then scripting)
  |-- Uses: libs/raytrace (rendering), libs/thread (multithreading)
```

The engine has no filesystem awareness. The client parses INI files, loads assets, builds data structs, and sends them to the engine via commands. The renderer (`libs/raytrace`) knows nothing beyond pixels.

Data flows one direction: commands in, pixels out.

## ECS Core

### Component Types (Bitmask)

```c
typedef enum {
    BF_COMP_NONE       = 0,
    BF_COMP_POSITION   = (1 << 0),
    BF_COMP_VISUAL     = (1 << 1),
    BF_COMP_LOCOMOTION = (1 << 2),
    BF_COMP_SELECTION  = (1 << 3),
} bf_component;
```

### Component Structs

```c
typedef struct {
    vector position;
    vector direction;
} bf_position;

typedef struct {
    int sprite_id;
    int anim_index;
    int anim_frame;
    float frame_timer;
    float anim_fps;
} bf_visual;

typedef enum {
    BF_LOCO_LINEAR,
    BF_LOCO_PARABOLIC,
    BF_LOCO_INSTANT,
} bf_loco_type;

typedef struct {
    bf_loco_type type;
    vector origin;
    vector target;
    float speed;
    float arc_height;   /* parabolic only */
    float progress;     /* 0.0 to 1.0 */
} bf_locomotion;

typedef struct {
    int selected;
} bf_selection;
```

### Storage (struct-of-arrays, inside bf_engine)

```c
#define MAX_ENTITIES 1024

uint32_t       component_masks[MAX_ENTITIES];
bf_position    positions[MAX_ENTITIES];
bf_visual      visuals[MAX_ENTITIES];
bf_locomotion  locomotions[MAX_ENTITIES];
bf_selection   selections[MAX_ENTITIES];
```

### Entity ID Management

Free list stack, same pattern as nbody:

```c
int free_stack[MAX_ENTITIES];
int free_top;
```

Entity creation pops from the free stack. Destruction pushes back. IDs are reusable indices.

### Component Lifetime

| Component | Lifetime | Notes |
|-----------|----------|-------|
| Position | Permanent | Every entity has this |
| Visual | Permanent | Set at spawn from unit def |
| Locomotion | Transient | Added by move commands, removed on arrival |
| Selection | Permanent | Tracks whether entity is selected |

Locomotion is the key transient component. An entity at rest has no Locomotion component. When a move command arrives, the engine adds Locomotion with type, target, speed. When the entity arrives, the Locomotion component is removed (bitmask cleared).

## Systems

Systems are static functions in `battleforge.c` that iterate entities by component mask. They run inside `bf_tick` in order:

### Locomotion System

Requires: `BF_COMP_POSITION | BF_COMP_LOCOMOTION`

Moves entities toward their target based on locomotion type:
- **LINEAR** — straight-line movement at speed, updates direction to face movement
- **PARABOLIC** — interpolates position along an arc using progress (0.0 to 1.0)
- **INSTANT** — sets position directly to target (teleport)

When `progress >= 1.0` or entity reaches target, removes `BF_COMP_LOCOMOTION` from the entity's mask.

### Terrain Snap System

Requires: `BF_COMP_POSITION | BF_COMP_LOCOMOTION`

Snaps entity Y to terrain height after locomotion updates. Only operates on moving entities (static entities already have correct Y from spawn).

### Animation System

Requires: `BF_COMP_VISUAL`

Advances frame timer, cycles animation frames. Same logic as current `bf_tick` animation code.

### Tick Order

```c
void bf_tick(bf_engine *e, float dt) {
    /* Process command queue */
    ...

    /* Run systems */
    system_locomotion(e, dt);
    system_terrain_snap(e);
    system_animation(e, dt);
}
```

### Render

`bf_render` iterates entities with `BF_COMP_POSITION | BF_COMP_VISUAL` and builds the raytrace scene. Same approach as current code but reads from component arrays instead of `bf_entity` fields.

## Unit Definitions

### bf_unit_def

```c
#define BF_UNIT_NAME_SIZE 32
#define MAX_UNIT_DEFS 256

typedef struct {
    char name[BF_UNIT_NAME_SIZE];
    int sprite_id;
    float base_speed;
} bf_unit_def;
```

- Stored by index (like `bf_register_sprite`)
- Name kept for debugging, logging, and console lookup
- `sprite_id` references a previously registered sprite
- `base_speed` used by the client when issuing move commands

### Registration

New command `BF_CMD_REGISTER_UNIT` carries a `bf_unit_def`. The engine stores it and returns the index.

### Entity Creation

`BF_CMD_ENTITY_CREATE` references a unit def by index. The engine looks up the def, derives the component mask from the def's data, and populates component arrays with defaults.

```c
/* entity_create now carries unit_def_id instead of sprite_id/speed */
struct { int id; int unit_def_id; vector position; vector direction; } entity_create;
```

The engine sets:
- `BF_COMP_POSITION` — always, from command's position/direction
- `BF_COMP_VISUAL` — if def has a valid sprite_id
- `BF_COMP_SELECTION` — if unit def INI has a `[selection]` section

`BF_COMP_LOCOMOTION` is not set at spawn. It is added when a move command arrives.

## Map Definitions

Maps are loaded from INI by the client and sent to the engine via `bf_set_map` (existing API, unchanged). The `bf_map` struct already carries all needed fields.

### Map INI Format

```ini
[map]
width = 100.0
depth = 100.0
grid_cols = 64
grid_rows = 64
max_height = 10.0

[lighting]
ambient = 0.15
light_dir = 1.0, 1.0, -1.0
light_intensity = 0.85

[terrain]
seed = 42
octaves = 6
lacunarity = 2.0
persistence = 0.5
```

The client parses this, builds a `bf_map` struct, calls `bf_map_generate_test_terrain` (or a future seed-based generator), and sends it via `bf_set_map`.

## Unit INI Format

```ini
[visual]
image = rifleman.png
angles = 16
columns = 4
frame_width = 64
frame_height = 64
fps = 8
width = 2.0
height = 2.0

[visual.animation.idle]
columns = 0
loop = true

[visual.animation.walk]
columns = 0,1,2,3
loop = true

[locomotion]
speed = 3.0

[selection]
```

The client parses this file, loads the sprite sheet from the `[visual]` section (using `libs/slice` which also uses `libs/ini`), registers the sprite with the engine, then builds a `bf_unit_def` with the sprite_id and base_speed, and sends `BF_CMD_REGISTER_UNIT`.

Sections map to components. Dot-separated sub-sections (e.g., `[visual.animation.idle]`) are nested data within that component.

## libs/ini

General-purpose INI parser supporting:
- Standard `[section]` headers
- Dot-separated section names: `[visual.animation.idle]`
- Key-value pairs: `key = value`
- Comments: lines starting with `;` or `#`
- Comma-separated value lists: `columns = 0,1,2,3`

### API

```c
typedef struct ini_file ini_file;

ini_file   *ini_load(const char *path);
void        ini_free(ini_file *ini);

const char *ini_get(const ini_file *ini, const char *section, const char *key);
int         ini_get_int(const ini_file *ini, const char *section, const char *key, int fallback);
float       ini_get_float(const ini_file *ini, const char *section, const char *key, float fallback);
int         ini_get_bool(const ini_file *ini, const char *section, const char *key, int fallback);

/* Iteration */
int         ini_section_count(const ini_file *ini);
const char *ini_section_name(const ini_file *ini, int index);
int         ini_key_count(const ini_file *ini, const char *section);
const char *ini_key_name(const ini_file *ini, const char *section, int index);
```

Comma-separated lists are parsed by the caller using `ini_get` and splitting on `,`.

### Refactor of libs/slice

`libs/slice` currently has its own INI parsing code. This will be refactored to use `libs/ini` instead, removing duplicate parsing logic.

## Commands (Updated)

```c
typedef enum {
    BF_CMD_CAMERA_SET,
    BF_CMD_CAMERA_MOVE,
    BF_CMD_REGISTER_UNIT,
    BF_CMD_ENTITY_CREATE,
    BF_CMD_ENTITY_DESTROY,
    BF_CMD_ENTITY_MOVE,
    BF_CMD_ENTITY_FACE,
    BF_CMD_SELECT,
    BF_CMD_ENTITY_ANIMATE,
    BF_CMD_COUNT
} bf_cmd_type;
```

Changes from current:
- Added `BF_CMD_REGISTER_UNIT`
- Removed `BF_CMD_ENTITY_SET_SPEED` — speed comes from unit def's `base_speed`, passed through move commands
- `BF_CMD_ENTITY_CREATE` now carries `unit_def_id` instead of `sprite_id` and `speed`
- `BF_CMD_ENTITY_MOVE` now carries `speed` (client reads from unit def) and locomotion type

```c
typedef struct {
    bf_cmd_type type;
    union {
        struct { vector position; vector direction; } camera_set;
        struct { vector delta; } camera_move;
        struct { bf_unit_def def; } register_unit;
        struct { int id; int unit_def_id; vector position; vector direction; } entity_create;
        struct { int id; } entity_destroy;
        struct { int id; vector target; float speed; bf_loco_type loco_type; } entity_move;
        struct { int id; vector direction; } entity_face;
        struct { int id; } select;
        struct { int id; int anim_index; } entity_animate;
    };
} bf_cmd;
```

## File Structure

### New files

```
libs/ini/
    ini.h              (public API)
    ini.c              (implementation)
    Makefile.am

units/                 (unit definition INI files)
    rifleman.ini
    scout.ini
    ...

maps/                  (map definition INI files)
    battlefield.ini
```

### Modified files

```
libs/battleforge/
    battleforge.h      (updated API — components, unit defs, commands)
    battleforge.c      (ECS refactor — component arrays, systems)

libs/slice/
    slice.c            (refactored to use libs/ini)

apps/barrier/
    main.c             (refactored to load INI files, use new commands)

configure.ac           (add libs/ini)
Makefile.am            (add libs/ini to SUBDIRS)
```

## Future Work (Designed, Not Implemented)

The following are documented here to ensure the architecture accommodates them. None are part of the first implementation pass.

### Additional Components

| Component | Fields | Lifetime |
|-----------|--------|----------|
| Health | current, max | Permanent |
| Stamina | current, max | Permanent |
| Morale | current, max | Permanent |
| Leadership | radius, morale_bonus | Permanent |
| Team | faction_id | Permanent |
| State | flags (bitmask) | Permanent |

### States (Bitmask)

```c
typedef enum {
    BF_STATE_IDLE      = (1 << 0),
    BF_STATE_MARCHING  = (1 << 1),
    BF_STATE_CHARGING  = (1 << 2),
    BF_STATE_ROOTED    = (1 << 3),
    BF_STATE_ROUTED    = (1 << 4),
    BF_STATE_STUNNED   = (1 << 5),
    BF_STATE_EXHAUSTED = (1 << 6),
    BF_STATE_DEAD      = (1 << 7),
} bf_state;
```

### Primitives (Engine Operations)

| Primitive | Effect |
|-----------|--------|
| Move | Adds/modifies Locomotion component |
| Damage | Modifies Health, may trigger state changes |
| Spawn | Creates a new entity (projectiles, effects) |
| Modify | Adds/removes components or changes state flags |
| Query | Spatial lookup — returns entities within radius |

### Ability Composition

Abilities are sequences of primitives defined in INI files. The engine provides a fixed schema of known ability keys. Examples:

| Ability | Composed from |
|---------|--------------|
| March | Move(linear, speed) |
| Charge | Move(linear, 2x speed) + Damage(contact) |
| Jump | Move(parabolic, arc) + Damage(area, on_land) |
| Knockback | Move(linear, forced) |
| Teleport | Move(instant) |
| Deploy | Modify(add rooted, remove locomotion capability) |
| Artillery | Spawn(projectile) |

### Scripting

INI files handle declaration and configuration. Logic (state interaction rules, ability conditions) starts as C code in the engine. A future scripting layer (likely Lua) replaces hardcoded rules, operating on the same component arrays and state system. The ECS architecture does not need to change for this — scripting just gets read/write access to existing component data.
