"""
Test script: generate a simple colored model, animate it,
and render a Slice-compatible sprite sheet.

Run headless: blender --background --python scripts/test_spritesheet.py

No external dependencies — uses only Blender built-ins.
"""

import bpy
import os
import math
import shutil
from mathutils import Vector

# --- Configuration ---
ANGLES = 16
CELL_SIZE = (64, 64)
FPS = 4
CAMERA_DISTANCE = 4.0
CAMERA_HEIGHT = 1.5
OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "..", "apps", "battleforge", "assets")
MODEL_NAME = "test_unit"

ANIMATIONS = {
    "idle":   ("IdleAction",   1, 1,  True),
    "walk":   ("WalkAction",   1, 4,  True),
}
# -----------------------


def clear_scene():
    """Remove all default objects."""
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()


def create_test_model():
    """Create a simple character: body (blue cube) + head (red sphere) + arm (green cube)."""
    # Body
    bpy.ops.mesh.primitive_cube_add(size=0.8, location=(0, 0, 0.6))
    body = bpy.context.active_object
    body.name = "Body"
    mat_body = bpy.data.materials.new("BodyMat")
    mat_body.use_nodes = True
    mat_body.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.2, 0.3, 0.8, 1.0)
    body.data.materials.append(mat_body)

    # Head
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.35, location=(0, 0, 1.35))
    head = bpy.context.active_object
    head.name = "Head"
    mat_head = bpy.data.materials.new("HeadMat")
    mat_head.use_nodes = True
    mat_head.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.9, 0.3, 0.2, 1.0)
    head.data.materials.append(mat_head)

    # Right arm (to break symmetry so angles are visually distinct)
    bpy.ops.mesh.primitive_cube_add(size=0.3, location=(0.6, 0, 0.7))
    arm = bpy.context.active_object
    arm.name = "RightArm"
    arm.scale = (0.4, 0.4, 1.2)
    mat_arm = bpy.data.materials.new("ArmMat")
    mat_arm.use_nodes = True
    mat_arm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.2, 0.7, 0.3, 1.0)
    arm.data.materials.append(mat_arm)

    # Parent head and arm to body
    head.parent = body
    arm.parent = body

    return body


def create_animations(body):
    """Create idle (static) and walk (bobbing) animations."""
    # Idle: single frame, no movement
    idle_action = bpy.data.actions.new("IdleAction")
    body.animation_data_create()
    body.animation_data.action = idle_action

    # Insert a single keyframe at frame 1
    body.location = (0, 0, 0)
    body.keyframe_insert(data_path="location", frame=1)

    # Walk: 4 frames of bobbing up and down
    walk_action = bpy.data.actions.new("WalkAction")
    body.animation_data.action = walk_action

    positions = [
        (1, (0, 0, 0)),
        (2, (0, 0, 0.15)),
        (3, (0, 0, 0)),
        (4, (0, 0, -0.1)),
    ]
    for frame, loc in positions:
        body.location = loc
        body.keyframe_insert(data_path="location", frame=frame)

    return idle_action, walk_action


def setup_camera():
    """Create orthographic camera."""
    cam_data = bpy.data.cameras.new("SpriteCamera")
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = 3.0

    cam_obj = bpy.data.objects.new("SpriteCamera", cam_data)
    bpy.context.scene.collection.objects.link(cam_obj)
    bpy.context.scene.camera = cam_obj
    return cam_obj


def position_camera(cam, angle_deg):
    """Place camera at angle, looking at origin."""
    rad = math.radians(-angle_deg)
    cam.location = Vector((
        math.sin(rad) * CAMERA_DISTANCE,
        -math.cos(rad) * CAMERA_DISTANCE,
        CAMERA_HEIGHT
    ))
    direction = Vector((0, 0, 0.6)) - cam.location
    rot_quat = direction.to_track_quat('-Z', 'Y')
    cam.rotation_euler = rot_quat.to_euler()


def setup_lighting():
    """Add a simple sun light."""
    light_data = bpy.data.lights.new("Sun", type='SUN')
    light_data.energy = 3.0
    light_obj = bpy.data.objects.new("Sun", light_data)
    light_obj.rotation_euler = (math.radians(45), 0, math.radians(30))
    bpy.context.scene.collection.objects.link(light_obj)


def render_frame_to(filepath):
    """Render current scene to filepath."""
    scene = bpy.context.scene
    scene.render.filepath = filepath
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.resolution_x = CELL_SIZE[0]
    scene.render.resolution_y = CELL_SIZE[1]
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.engine = 'BLENDER_EEVEE'
    bpy.ops.render.render(write_still=True)


def composite_sheet(frame_paths, total_cols, total_rows, output_path):
    """Stitch renders into a sprite sheet."""
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
                    dst_y = (total_rows - 1 - row) * h + y
                    dst_x = col * w + x
                    dst_i = (dst_y * sheet_w + dst_x) * 4
                    pixels[dst_i:dst_i+4] = src[src_i:src_i+4]

    sheet.pixels = pixels
    sheet.filepath_raw = output_path
    sheet.file_format = 'PNG'
    sheet.save()
    bpy.data.images.remove(sheet)


def generate_ini(output_path):
    """Write INI sidecar."""
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
    print("=== Slice Sprite Sheet Test ===")

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    tmp_dir = os.path.join(OUTPUT_DIR, "_tmp_frames")
    os.makedirs(tmp_dir, exist_ok=True)

    clear_scene()
    body = create_test_model()
    idle_action, walk_action = create_animations(body)
    cam = setup_camera()
    setup_lighting()

    total_cols = sum(end - start + 1 for _, (_, start, end, _) in ANIMATIONS.items())
    total_rows = ANGLES
    frame_paths = [None] * (total_rows * total_cols)

    print(f"Rendering {total_rows} angles x {total_cols} columns = {total_rows * total_cols} frames...")

    col_offset = 0
    for anim_name, (action_name, start, end, _) in ANIMATIONS.items():
        body.animation_data.action = bpy.data.actions[action_name]

        frame_count = end - start + 1
        for frame_idx in range(frame_count):
            bpy.context.scene.frame_set(start + frame_idx)
            col = col_offset + frame_idx

            for angle_idx in range(ANGLES):
                angle_deg = angle_idx * (360.0 / ANGLES)
                position_camera(cam, angle_deg)

                filename = f"{angle_idx:02d}_{col:03d}.png"
                filepath = os.path.join(tmp_dir, filename)
                render_frame_to(filepath)
                frame_paths[angle_idx * total_cols + col] = filepath

            print(f"  {anim_name} frame {frame_idx + 1}/{frame_count} done")

        col_offset += frame_count

    png_path = os.path.join(OUTPUT_DIR, f"{MODEL_NAME}.png")
    composite_sheet(frame_paths, total_cols, total_rows, png_path)

    ini_path = os.path.join(OUTPUT_DIR, f"{MODEL_NAME}.ini")
    generate_ini(ini_path)

    shutil.rmtree(tmp_dir, ignore_errors=True)

    print(f"\nDone!")
    print(f"  PNG: {png_path}")
    print(f"  INI: {ini_path}")
    print(f"  Sheet: {total_cols * CELL_SIZE[0]}x{total_rows * CELL_SIZE[1]} "
          f"({total_cols} cols x {total_rows} rows)")


main()
