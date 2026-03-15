# Slice — Sprite Sheet Loader & Animation Engine

**Date:** 2026-03-15
**Status:** Draft

## Purpose

Replace procedurally generated sprites with artist-produced PNG sprite sheets. Provide a loader library (`libs/slice`) that reads PNG + INI sidecar files, and extend the battleforge engine with animation playback.

## Sheet Layout

Sprite sheets are PNG images laid out as a grid:

```
              col 0    col 1    col 2    col 3   ...
row 0  (0°)    [  ]     [  ]     [  ]     [  ]
row 1  (22.5°) [  ]     [  ]     [  ]     [  ]
row 2  (45°)   [  ]     [  ]     [  ]     [  ]
...
row 15 (337.5°)[  ]     [  ]     [  ]     [  ]
```

- Rows = viewing angles, clockwise from front (matching existing `rt_sprite` convention)
- Columns = animation frames
- All cells are the same size (`frame_width` x `frame_height`)
- Transparent pixels use alpha = 0

The number of rows must match the `angles` value in the INI file. The number of columns is derived from the PNG width divided by `frame_width`.

## INI Sidecar Format

For a sprite sheet `warrior.png`, the sidecar is `warrior.ini`:

```ini
frame_width=32
frame_height=32
angles=16
fps=8

[idle]
frames=0

[walk]
frames=0,1,2,3

[attack]
frames=4,5,6,7

[death]
frames=8,9,10
loop=false
```

### Required fields

- `frame_width` — pixel width of each cell
- `frame_height` — pixel height of each cell
- `angles` — number of rows (viewing angles)
- `fps` — default animation playback speed (frames per second)

### Animation sections

Each `[name]` section defines a named animation:

- `frames` — comma-separated list of column indices into the sheet
- `loop` — optional, defaults to `true`. If `false`, animation plays once and holds the last frame.

## Slice Library API (`libs/slice`)

### Files

- `slice.h` — public API
- `slice.c` — implementation (PNG loading, hand-rolled INI parsing, sheet slicing)
- `stb_image.h` — vendored single-header image loader (public domain)

The INI parser is hand-rolled (~100 lines). The format is simple enough that no library is needed.

### Types

```c
typedef struct {
    char name[32];          /* animation name from INI section */
    int *columns;           /* column indices for this animation */
    int column_count;       /* number of frames in animation */
    int loop;               /* 1 = loop, 0 = play once and hold */
} slice_anim;

typedef struct {
    uint32_t **pixels;      /* [angles * total_columns] pixel buffers, row-major */
    int angles;             /* number of viewing angles (rows) */
    int total_columns;      /* total columns in the sheet */
    int frame_width;        /* pixel width per frame */
    int frame_height;       /* pixel height per frame */
    float fps;              /* default playback speed */
    slice_anim *anims;      /* named animations */
    int anim_count;         /* number of animations */
} slice_sheet;
```

### Functions

```c
/* Load sprite sheet from PNG + INI sidecar.
   Finds INI by replacing .png extension with .ini.
   Returns NULL on failure (logs reason to stderr). */
slice_sheet *slice_load(const char *png_path);

/* Free all memory owned by the sheet. */
void slice_free(slice_sheet *sheet);

/* Look up animation index by name. Returns -1 if not found. */
int slice_anim_index(const slice_sheet *sheet, const char *name);
```

### Pixel format conversion

`stb_image` returns RGBA as raw bytes `[R][G][B][A]`. The raytracer expects ARGB8888 as `uint32_t`. The loader converts each pixel during slicing:

```c
uint8_t *src = &rgba[4 * (src_y * img_width + src_x)];
uint32_t argb = ((uint32_t)src[3] << 24) | ((uint32_t)src[0] << 16)
              | ((uint32_t)src[1] << 8)  |  (uint32_t)src[2];
```

Each cell is extracted into its own `malloc`'d `uint32_t` buffer, stored in `pixels[row * total_columns + col]`.

## Battleforge Engine Changes

### New entity animation state

Added to the internal `bf_entity` struct:

```c
typedef struct {
    /* ...existing fields... */
    int anim_index;         /* current animation index (-1 = none) */
    int anim_frame;         /* current position within animation's column list */
    float frame_timer;      /* time accumulator */
    float anim_fps;         /* playback speed (copied from sheet default) */
} bf_entity;
```

### New command: `BF_CMD_ENTITY_ANIMATE`

```c
struct { int id; int anim_index; } entity_animate;
```

Sets the entity's current animation. Resets `anim_frame` to 0 and `frame_timer` to 0.

### Changed registration: `bf_register_sprite`

The existing `bf_register_sprite` changes signature to accept a `slice_sheet*`:

```c
int bf_register_sprite(bf_engine *e, slice_sheet *sheet, float world_width, float world_height);
```

Stores the sheet pointer alongside world dimensions. Returns a sprite ID usable with `BF_CMD_ENTITY_CREATE`. The old `bf_sprite_def` type is removed.

**Memory ownership:** The engine does NOT take ownership of the `slice_sheet`. The caller must keep it alive for the engine's lifetime and free it after `bf_destroy`. New entities created with a sheet sprite default to `anim_index = -1` (no animation), `anim_frame = 0`, `frame_timer = 0`.

### Tick changes

In `bf_tick`, after processing commands and advancing movement, advance animation:

```
for each active entity with anim_index >= 0:
    frame_timer += dt
    if frame_timer >= 1.0 / anim_fps:
        frame_timer -= 1.0 / anim_fps
        anim_frame++
        if anim_frame >= anim.column_count:
            if anim.loop:
                anim_frame = 0
            else:
                anim_frame = column_count - 1   // hold last frame
```

### Render changes

The engine selects the animation frame column, builds a full angle row, and lets the raytracer handle angle selection (no duplicated math).

In `bf_render`, for sheet-based sprites:

```c
/* 1. Determine current animation column */
int col = anim->columns[ent->anim_frame];

/* 2. Build stack-allocated rt_frame array: one per angle */
rt_frame frames[sheet->angles];  /* VLA, max 32 — safe for stack */
for (int a = 0; a < sheet->angles; a++) {
    frames[a] = (rt_frame){
        .pixels = sheet->pixels[a * sheet->total_columns + col],
        .width  = sheet->frame_width,
        .height = sheet->frame_height
    };
}

/* 3. Pass to raytracer — it selects the angle as usual */
rt_scene_add_sprite(e->scene, (rt_sprite){
    .position    = ent->position,
    .direction   = ent->direction,
    .width       = entry->width,
    .height      = entry->height,
    .frame_count = sheet->angles,
    .frames      = frames
});
```

All sprites follow this same path — no branching needed.

### Sprite storage changes

The engine stores all sprites uniformly — no tagged union needed:

```c
typedef struct {
    slice_sheet *sheet;     /* borrowed pointer to loaded sheet */
    float width;            /* world-space width */
    float height;           /* world-space height */
} bf_sprite_entry;
```

Every registered sprite is a `slice_sheet`. The old `bf_sprite_def` type is removed from the public API.

## Build Integration

### New files

```
libs/slice/
├── Makefile.am
├── slice.h
├── slice.c
└── stb_image.h
```

### Makefile.am

```makefile
noinst_LTLIBRARIES = libslice.la
libslice_la_SOURCES = slice.c
libslice_la_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace
```

### Root Makefile.am

Add `libs/slice` to `SUBDIRS` before `libs/battleforge`. Update battleforge's Makefile.am to link `libslice.la` and add the include path.

### configure.ac

Add `libs/slice/Makefile` to `AC_CONFIG_FILES` so autotools generates the Makefile for the new subdirectory.

### Affected code paths

All code paths that access `e->sprites[...]` switch to the new `bf_sprite_entry`:

- **`bf_render`** — builds angle-row `rt_frame` array from sheet (as shown above)
- **`bf_pick`** — same frame-building logic to construct the `rt_sprite` for picking
- **`bf_register_sprite`** — new signature, stores `slice_sheet*` + world dimensions
- **`cmd_entity_create`** — initializes `anim_index = -1` for new entities

A helper function `build_sprite_frames(bf_engine *e, bf_entity *ent, rt_frame *out_frames)` should be extracted to share the frame-building logic between `bf_render` and `bf_pick`.

## What stays the same

- `rt_frame`, `rt_sprite`, and all raytracer internals — unchanged
- All existing commands — unchanged
- The shell's camera controls and input handling — unchanged
- Setup vs runtime split — direct function calls for init, commands for gameplay

## Dependencies

- `stb_image.h` — vendored, public domain, no external dependency
- No other new dependencies
