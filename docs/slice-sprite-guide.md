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

**Setup (one-time):**

1. Install Blender (blender.org)
2. Save the script below as `render_spritesheet.py`

**Per-character workflow:**

1. Open your 3D model in Blender (supports .blend, .fbx, .glTF, .obj, etc.)
2. Create your animations as Blender Actions (idle, walk, attack, etc.)
3. Set up rendering:
   - Switch to a transparent background: `Render Properties > Film > Transparent`
   - Set render resolution to your cell size (e.g., 128x128 — you can downscale later)
   - Use EEVEE for fast renders, Cycles for higher quality
4. Run the script: `Edit > Preferences > File Paths` set your output directory, then in the Scripting workspace, open and run `render_spritesheet.py`
5. The script outputs one PNG sprite sheet + one INI file per character

**`render_spritesheet.py`:**

```python
"""
Render a 3D model from multiple angles and animation frames
into a Slice-compatible sprite sheet (PNG + INI).

Usage:
  1. Open your .blend file with the model at the origin
  2. Set ANGLES, CELL_SIZE, FPS, and ANIMATIONS below
  3. Run this script from Blender's Scripting workspace
  4. Output: <model_name>.png + <model_name>.ini in OUTPUT_DIR
"""

import bpy
import os
import math
from mathutils import Vector

# --- Configuration ---
ANGLES = 16                    # number of viewing angles (rows)
CELL_SIZE = (128, 128)         # render resolution per frame (width, height)
FPS = 8                        # animation playback speed
CAMERA_DISTANCE = 5.0          # distance from model center
CAMERA_HEIGHT = 2.0            # camera elevation
OUTPUT_DIR = "//sprites"       # output path (// = relative to .blend file)
MODEL_NAME = "warrior"         # output filename

# Define animations: name -> (action_name, start_frame, end_frame, loop)
ANIMATIONS = {
    "idle":   ("Idle",   1, 1,  True),
    "walk":   ("Walk",   1, 8,  True),
    "attack": ("Attack", 1, 6,  True),
    "death":  ("Death",  1, 4,  False),
}
# -----------------------

def setup_camera():
    """Create or reuse an orthographic camera for consistent sprite rendering."""
    cam_data = bpy.data.cameras.get("SpriteCamera")
    if not cam_data:
        cam_data = bpy.data.cameras.new("SpriteCamera")
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = 3.0

    cam_obj = bpy.data.objects.get("SpriteCamera")
    if not cam_obj:
        cam_obj = bpy.data.objects.new("SpriteCamera", cam_data)
        bpy.context.scene.collection.objects.link(cam_obj)

    bpy.context.scene.camera = cam_obj
    return cam_obj


def position_camera(cam, angle_deg):
    """Place camera at given angle (degrees clockwise from front), looking at origin."""
    rad = math.radians(angle_deg)
    cam.location = Vector((
        math.sin(rad) * CAMERA_DISTANCE,
        -math.cos(rad) * CAMERA_DISTANCE,
        CAMERA_HEIGHT
    ))
    direction = Vector((0, 0, 0)) - cam.location
    rot_quat = direction.to_track_quat('-Z', 'Y')
    cam.rotation_euler = rot_quat.to_euler()


def render_frame(filepath):
    """Render current scene to filepath as RGBA PNG."""
    scene = bpy.context.scene
    scene.render.filepath = filepath
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.resolution_x = CELL_SIZE[0]
    scene.render.resolution_y = CELL_SIZE[1]
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    bpy.ops.render.render(write_still=True)


def composite_sheet(frame_paths, total_cols, total_rows, output_path):
    """Stitch individual renders into a single sprite sheet PNG."""
    # Use Blender's compositor or Pillow-free approach via bpy.data.images
    w, h = CELL_SIZE
    sheet_w = total_cols * w
    sheet_h = total_rows * h

    sheet = bpy.data.images.new("SpriteSheet", sheet_w, sheet_h, alpha=True)
    pixels = [0.0] * (sheet_w * sheet_h * 4)

    for row in range(total_rows):
        for col in range(total_cols):
            idx = row * total_cols + col
            if idx >= len(frame_paths) or frame_paths[idx] is None:
                continue

            img = bpy.data.images.load(frame_paths[idx])
            src = list(img.pixels)
            bpy.data.images.remove(img)

            for y in range(h):
                for x in range(w):
                    src_i = (y * w + x) * 4
                    # Flip Y: Blender images are bottom-up, sheet is top-down
                    dst_y = (total_rows - 1 - row) * h + (h - 1 - y)
                    dst_x = col * w + x
                    dst_i = (dst_y * sheet_w + dst_x) * 4
                    pixels[dst_i:dst_i+4] = src[src_i:src_i+4]

    sheet.pixels = pixels
    sheet.filepath_raw = output_path
    sheet.file_format = 'PNG'
    sheet.save()
    bpy.data.images.remove(sheet)


def generate_ini(output_path, total_cols):
    """Write the INI sidecar file."""
    col_offset = 0
    with open(output_path, 'w') as f:
        f.write(f"frame_width={CELL_SIZE[0]}\n")
        f.write(f"frame_height={CELL_SIZE[1]}\n")
        f.write(f"angles={ANGLES}\n")
        f.write(f"fps={FPS}\n")

        for name, (action, start, end, loop) in ANIMATIONS.items():
            frame_count = end - start + 1
            cols = list(range(col_offset, col_offset + frame_count))
            f.write(f"\n[{name}]\n")
            f.write(f"frames={','.join(str(c) for c in cols)}\n")
            if not loop:
                f.write("loop=false\n")
            col_offset += frame_count


def main():
    out_dir = bpy.path.abspath(OUTPUT_DIR)
    os.makedirs(out_dir, exist_ok=True)
    tmp_dir = os.path.join(out_dir, "_tmp_frames")
    os.makedirs(tmp_dir, exist_ok=True)

    cam = setup_camera()
    armature = None
    for obj in bpy.context.scene.objects:
        if obj.type == 'ARMATURE':
            armature = obj
            break

    # Compute total columns (sum of all animation frame counts)
    total_cols = sum(end - start + 1 for _, (_, start, end, _) in ANIMATIONS.items())
    total_rows = ANGLES
    frame_paths = [None] * (total_rows * total_cols)

    col_offset = 0
    for anim_name, (action_name, start, end, _) in ANIMATIONS.items():
        # Set animation
        if armature and action_name in bpy.data.actions:
            armature.animation_data.action = bpy.data.actions[action_name]

        frame_count = end - start + 1
        for frame_idx in range(frame_count):
            bpy.context.scene.frame_set(start + frame_idx)
            col = col_offset + frame_idx

            for angle_idx in range(ANGLES):
                angle_deg = angle_idx * (360.0 / ANGLES)
                position_camera(cam, angle_deg)

                filename = f"{angle_idx:02d}_{col:03d}.png"
                filepath = os.path.join(tmp_dir, filename)
                render_frame(filepath)
                frame_paths[angle_idx * total_cols + col] = filepath

        col_offset += frame_count

    # Composite into sheet
    png_path = os.path.join(out_dir, f"{MODEL_NAME}.png")
    composite_sheet(frame_paths, total_cols, total_rows, png_path)

    # Generate INI
    ini_path = os.path.join(out_dir, f"{MODEL_NAME}.ini")
    generate_ini(ini_path, total_cols)

    # Cleanup temp frames
    import shutil
    shutil.rmtree(tmp_dir, ignore_errors=True)

    print(f"Done: {png_path} + {ini_path}")
    print(f"Sheet: {total_cols * CELL_SIZE[0]}x{total_rows * CELL_SIZE[1]} "
          f"({total_cols} cols x {total_rows} rows)")


main()
```

**What the script does:**

1. Creates an orthographic camera
2. For each animation, sets the Blender action and iterates through frames
3. For each frame, orbits the camera through all angles (0°, 22.5°, 45°, ...)
4. Renders each angle+frame as an individual transparent PNG
5. Composites all renders into a single sprite sheet
6. Generates the matching INI file automatically
7. Cleans up temporary files

**Configuration (top of script):**

| Variable | Description |
|----------|-------------|
| `ANGLES` | Number of viewing angles (16 or 32) |
| `CELL_SIZE` | Pixel size per frame, e.g., `(128, 128)` or `(64, 64)` |
| `FPS` | Animation playback speed |
| `CAMERA_DISTANCE` | How far the camera orbits from center |
| `CAMERA_HEIGHT` | Camera elevation (adjust per model) |
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
