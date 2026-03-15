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

- **Blender** (free, open source) — 3D modeling, animation, and automated sprite sheet rendering
- **LibreSprite** (free, open source) — pixel art editor with animation timeline and sprite sheet export
- **GIMP** (free) — general image editor, manual grid layout
- **Piskel** (free, browser-based) — quick pixel art prototyping, no install needed

### 3D Model to Sprite Sheet (Blender)

This is the recommended workflow for production sprites. You create or import a 3D model, animate it, and use a script to render every angle x frame into a sprite sheet automatically.

**Requirements:** Blender (free, blender.org). The script uses only Blender's built-in Python — no external dependencies or pip installs needed.

**Setup (one-time):**

1. Install Blender
2. The render script is at `scripts/render_spritesheet.py` in this repo

**Per-character workflow:**

1. Open your 3D model in Blender (supports .blend, .fbx, .glTF, .obj, etc.)
2. Create your animations as Blender Actions (idle, walk, attack, etc.)
3. Set up rendering:
   - Switch to a transparent background: `Render Properties > Film > Transparent`
   - Use EEVEE for fast renders, Cycles for higher quality
4. Edit the configuration at the top of `scripts/render_spritesheet.py`:
   - Set `MODEL_NAME`, `ANGLES`, `CELL_SIZE`, `FPS`
   - Define your `ANIMATIONS` dict mapping names to Blender actions and frame ranges
5. In Blender's Scripting workspace, open and run `scripts/render_spritesheet.py`
6. The script outputs a PNG sprite sheet + INI file in the output directory

**What the script does:**

1. Creates an orthographic camera
2. For each animation, sets the Blender action and iterates through frames
3. For each frame, orbits the camera through all angles (0°, 22.5°, 45°, ...)
4. Renders each angle+frame as an individual transparent PNG
5. Composites all renders into a single sprite sheet
6. Generates the matching INI file automatically
7. Cleans up temporary files

**Configuration (top of `scripts/render_spritesheet.py`):**

| Variable | Description |
|----------|-------------|
| `ANGLES` | Number of viewing angles (16 or 32) |
| `CELL_SIZE` | Pixel size per frame, e.g., `(128, 128)` or `(64, 64)` |
| `FPS` | Animation playback speed |
| `CAMERA_DISTANCE` | How far the camera orbits from center |
| `CAMERA_HEIGHT` | Camera elevation (adjust per model) |
| `OUTPUT_DIR` | Where to write output (`//` = relative to .blend file) |
| `MODEL_NAME` | Output filename (produces `<name>.png` + `<name>.ini`) |
| `ANIMATIONS` | Dict mapping animation names to Blender actions and frame ranges |

**Tips for 3D models:**

- Place the model at the world origin, facing **-Y** (Blender's default forward)
- Use an orthographic camera for clean, consistent sprites
- Adjust `CAMERA_DISTANCE` and `ortho_scale` so the model fills the cell with some padding
- Render at 2x your target cell size (e.g., 128x128) then downscale to 64x64 for crisper pixels
- For pixel art style, apply a toon/cel shader and reduce colors in post

**Where to get free 3D models:**

- **Mixamo** (free, Adobe account) — rigged humanoid characters with animations
- **Kenney** (kenney.nl) — free game assets, CC0 licensed
- **OpenGameArt** (opengameart.org) — community-contributed game assets
- **Quaternius** (quaternius.com) — free low-poly character packs

**Mixamo workflow (recommended for quick results):**

1. Go to mixamo.com, pick a character
2. Apply animations (idle, walk, attack, etc.) and download each as .fbx
3. Import into Blender: `File > Import > FBX`
4. Rename each imported action to match your `ANIMATIONS` config
5. Run the script

### Pixel Art Workflow (LibreSprite)

For hand-drawn pixel art sprites:

1. Create a new sprite at your cell size (e.g., 32x32)
2. Draw the front-facing idle frame
3. Duplicate and modify for each angle (use rotation as a starting point, then hand-correct)
4. Add animation frames as additional columns
5. Export: `File > Export Sprite Sheet`
   - Layout: "By Rows"
   - Constraints: Fixed columns = number of animation frames
6. Write the INI file by hand

### General Tips

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
