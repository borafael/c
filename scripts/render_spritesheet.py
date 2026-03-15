"""
Render a 3D model from multiple angles and animation frames
into a Slice-compatible sprite sheet (PNG + INI).

No external dependencies — runs entirely within Blender's built-in Python.

Usage:
  1. Open your .blend file with the model at the origin
  2. Set ANGLES, CELL_SIZE, FPS, and ANIMATIONS below
  3. Run this script from Blender's Scripting workspace
  4. Output: <model_name>.png + <model_name>.ini in OUTPUT_DIR
"""

import bpy
import os
import math
import shutil
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
    shutil.rmtree(tmp_dir, ignore_errors=True)

    print(f"Done: {png_path} + {ini_path}")
    print(f"Sheet: {total_cols * CELL_SIZE[0]}x{total_rows * CELL_SIZE[1]} "
          f"({total_cols} cols x {total_rows} rows)")


main()
