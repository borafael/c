# Slice Sprite Guide

How to create and use sprite sheets for the Battleforge engine.

## Quick Start

1. Create a PNG sprite sheet
2. Create a matching INI file
3. Drop both in `apps/battleforge/assets/`
4. Load in code with `slice_load("assets/warrior.png")`

## Sprite Sheet Layout

Your PNG is a grid. **Rows are viewing angles**, **columns are animation frames**.

```
              col 0    col 1    col 2    col 3
row 0  (0°)    [idle]   [walk1]  [walk2]  [attack]
row 1  (22.5°) [idle]   [walk1]  [walk2]  [attack]
row 2  (45°)   [idle]   [walk1]  [walk2]  [attack]
...
row 15 (337.5°)[idle]   [walk1]  [walk2]  [attack]
```

### Angles

Angles go **clockwise from front** (the direction the entity faces):

```
         0° (front)
   337.5°     22.5°
  315°           45°
 292.5°           67.5°
  270°             90°
   247.5°        112.5°
    225°        135°
      202.5°  157.5°
        180° (back)
```

With 16 angles, each row covers 22.5 degrees. The engine picks the closest row based on the camera's position relative to the entity.

### Rules

- All cells must be the **same size** (e.g., 32x32, 64x64)
- Use **transparent pixels** (alpha = 0) for empty space around the sprite
- PNG must have an **alpha channel** (RGBA)
- Rows x columns must fill the entire image (no partial rows)

## INI Sidecar File

For every `sprite.png`, create a `sprite.ini` in the same directory.

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

### Global Settings

| Key | Description |
|-----|-------------|
| `frame_width` | Pixel width of each cell |
| `frame_height` | Pixel height of each cell |
| `angles` | Number of rows (viewing angles): 16 or 32 |
| `fps` | Animation playback speed (frames per second) |

### Animation Sections

Each `[name]` block defines a named animation:

| Key | Description | Default |
|-----|-------------|---------|
| `frames` | Comma-separated column indices | (required) |
| `loop` | `true` or `false` | `true` |

Column indices refer to columns in the sprite sheet (0-based). A single column like `frames=0` means a static pose. Multiple columns like `frames=0,1,2,3` create an animated sequence.

When `loop=false`, the animation plays once and holds the last frame.

## Creating Sprites

### Recommended Tools

- **Aseprite** ($20, best for pixel art) — draw sprites, export as PNG sheet
- **LibreSprite** (free, Aseprite fork) — same workflow, open source
- **GIMP** (free) — manual grid layout
- **Piskel** (free, browser) — quick pixel art prototyping

### Workflow with Aseprite

1. Create a new sprite at your cell size (e.g., 32x32)
2. Draw the front-facing idle frame
3. Duplicate and modify for each angle (use Aseprite's rotation as a starting point, then hand-correct)
4. Add animation frames as additional columns
5. Export: `File > Export Sprite Sheet`
   - Layout: "By Rows"
   - Constraints: Fixed columns = number of animation frames
6. Write the INI file by hand

### Tips

- Start with 4 or 8 angles to prototype, then fill in the intermediates
- Mirror left-facing frames from right-facing to save work (just flip horizontally)
- Keep sprites centered in their cell — the engine renders from the center point
- Use high-contrast outlines for readability at small sizes

## Loading in Code

### From a file (production use)

```c
#include "slice.h"
#include "battleforge.h"

/* Load the sprite sheet */
slice_sheet *warrior = slice_load("assets/warrior.png");
if (!warrior) {
    fprintf(stderr, "Failed to load warrior sprite\n");
    return 1;
}

/* Register with engine (2x2 world units) */
int spr_id = bf_register_sprite(engine, warrior, 2.0f, 2.0f);

/* Create an entity using this sprite */
bf_command(engine, (bf_cmd){
    .type = BF_CMD_ENTITY_CREATE,
    .entity_create = {
        .id = 1,
        .sprite_id = spr_id,
        .position = {0.0f, 1.0f, 0.0f},
        .direction = {0.0f, 0.0f, 1.0f},
        .speed = 3.0f
    }
});

/* Play the walk animation */
int walk_idx = slice_anim_index(warrior, "walk");
bf_command(engine, (bf_cmd){
    .type = BF_CMD_ENTITY_ANIMATE,
    .entity_animate = { .id = 1, .anim_index = walk_idx }
});
```

### Memory ownership

The engine **borrows** the `slice_sheet` pointer — it does not copy it. You must keep the sheet alive for the engine's lifetime:

```c
/* Correct order: destroy engine first, then free sheets */
bf_destroy(engine);
slice_free(warrior);
```

### World size

The `world_width` and `world_height` parameters in `bf_register_sprite` control how large the sprite appears in the 3D scene. A 2x2 sprite occupies 2 world units wide and 2 tall. Adjust to taste.

## File Organization

```
apps/battleforge/
├── main.c
└── assets/
    ├── warrior.png
    ├── warrior.ini
    ├── archer.png
    ├── archer.ini
    └── ...
```

Keep PNG and INI files side by side with matching names. The loader finds the INI automatically by replacing `.png` with `.ini`.

## Troubleshooting

| Error message | Cause | Fix |
|---------------|-------|-----|
| `slice: cannot open INI 'foo.ini'` | INI file missing or wrong name | Ensure `foo.ini` exists next to `foo.png` |
| `slice: invalid INI values` | Missing or zero values in INI | Check all 4 global keys are set and positive |
| `slice: PNG has N rows but INI expects M angles` | Image too short for angle count | Add more rows or reduce `angles` in INI |
| `slice: cannot load PNG` | File not found or corrupt | Check path and that the PNG is valid RGBA |
| Sprite looks wrong/garbled | Cell size mismatch | Verify `frame_width` x `frame_height` match your actual cell size |
| Sprite faces wrong direction | Angle order wrong | Row 0 = front (facing camera), clockwise from there |

## Reference

### Supported formats

The loader uses stb_image internally, so it supports: **PNG** (recommended), BMP, TGA, JPEG, GIF, PSD, HDR, PIC. Always use PNG for sprites (it supports transparency).

### Limits

- Max 256 registered sprites per engine
- Max 1024 entities per engine
- Max 32 viewing angles per sheet
- Max 64 named animations per sheet
- Animation names max 31 characters
