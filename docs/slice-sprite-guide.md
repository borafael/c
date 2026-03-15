# Slice Sprite Guide

How to create sprite sheets and INI files for the Slice loader.

## Quick Start

1. Create a PNG sprite sheet (grid of frames)
2. Create a matching INI file with the same name
3. Place both files in the same directory

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

Angles go **clockwise from front** (the direction the character faces):

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

With 16 angles, each row covers 22.5 degrees. With 32 angles, each row covers 11.25 degrees. Start with 16 — you can always add more later.

### Rules

- All cells must be the **same size** (e.g., 32x32, 64x64)
- Use **transparent pixels** (alpha = 0) for empty space around the character
- PNG must have an **alpha channel** (RGBA)
- Rows x columns must fill the entire image — no partial rows

## INI File

For every `sprite.png`, create a `sprite.ini` in the same directory. The names must match (only the extension changes).

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

| Key | Description | Example |
|-----|-------------|---------|
| `frame_width` | Pixel width of each cell | `32` |
| `frame_height` | Pixel height of each cell | `32` |
| `angles` | Number of rows (viewing angles) | `16` or `32` |
| `fps` | Animation playback speed (frames per second) | `8` |

All four are required. Must be positive values.

### Animation Sections

Each `[name]` block defines a named animation:

| Key | Description | Default |
|-----|-------------|---------|
| `frames` | Comma-separated column indices (0-based) | (required) |
| `loop` | `true` or `false` | `true` |

- `frames=0` — a static pose (single column)
- `frames=0,1,2,3` — a 4-frame animated sequence
- `loop=false` — animation plays once and holds the last frame (useful for death animations)

You can define as many animations as you need. Name them whatever makes sense for the character.

## Recommended Tools

- **LibreSprite** (free, open source) — pixel art editor with animation timeline and sprite sheet export
- **GIMP** (free) — general image editor, manual grid layout
- **Piskel** (free, browser-based) — quick pixel art prototyping, no install needed

### Workflow with LibreSprite

1. Create a new sprite at your cell size (e.g., 32x32)
2. Draw the front-facing idle frame
3. Duplicate and modify for each angle (use rotation as a starting point, then hand-correct)
4. Add animation frames as additional columns
5. Export: `File > Export Sprite Sheet`
   - Layout: "By Rows"
   - Constraints: Fixed columns = number of animation frames
6. Write the INI file by hand

### Tips

- Start with 4 or 8 angles to prototype, then fill in the intermediates
- Mirror left-facing frames from right-facing to save work (just flip horizontally)
- Keep sprites centered in their cell
- Use high-contrast outlines for readability at small sizes
- PNG is the recommended format (supports transparency), but BMP, TGA, and JPEG also work

## Example

A warrior with 16 angles and 11 animation columns (1 idle + 4 walk + 4 attack + 2 death + 1 spare):

**`warrior.png`** — 352x512 image (11 columns x 32px wide, 16 rows x 32px tall)

**`warrior.ini`**:
```ini
frame_width=32
frame_height=32
angles=16
fps=8

[idle]
frames=0

[walk]
frames=1,2,3,4

[attack]
frames=5,6,7,8

[death]
frames=9,10
loop=false
```

## Checklist

Before handing off your sprites:

- [ ] PNG has alpha channel (save as RGBA, not RGB)
- [ ] All cells are the same size
- [ ] Number of rows matches `angles` in the INI
- [ ] PNG width is evenly divisible by `frame_width`
- [ ] PNG height is evenly divisible by `frame_height`
- [ ] INI file has all 4 global keys (`frame_width`, `frame_height`, `angles`, `fps`)
- [ ] Every animation section has a `frames` key
- [ ] Column indices in `frames` don't exceed the number of columns in the sheet
- [ ] INI file has the same name as the PNG (just different extension)
- [ ] Both files are in the same directory

## Limits

- Max 32 viewing angles per sheet
- Max 64 named animations per sheet
- Animation names max 31 characters
