#!/usr/bin/env python3
"""Generate 20 sci-fi prototype sprite sheets for Barrier.

Each sprite sheet is a PNG with:
  - 1 column (idle frame only)
  - 32 rows (one per viewing angle, 11.25 deg increments clockwise from front)
  - 16x16 pixels per frame, RGBA

An accompanying .ini file is generated for the slice loader.

Usage:
    pip install Pillow
    python generate_sprites.py
"""

import math
from PIL import Image, ImageDraw

S = 16        # frame size
ANGLES = 32   # viewing angles

def rot(cx, cy, px, py, angle):
    """Rotate point (px,py) around (cx,cy) by angle radians."""
    s, c = math.sin(angle), math.cos(angle)
    dx, dy = px - cx, py - cy
    return cx + dx * c - dy * s, cy + dx * s + dy * c


def draw_unit(draw, ox, oy, angle_idx, unit_type):
    """Draw a single unit frame at offset (ox, oy) for given angle index."""
    angle = -(angle_idx / ANGLES) * 2 * math.pi  # viewing angle (negated for billboard)
    cx, cy = ox + 7, oy + 7  # center of 16x16 frame

    UNITS[unit_type](draw, ox, oy, cx, cy, angle)


def _circle(draw, cx, cy, r, fill):
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)


def _rect(draw, x0, y0, x1, y1, fill):
    draw.rectangle([x0, y0, x1, y1], fill=fill)


def _line_at_angle(draw, cx, cy, angle, r_start, r_end, fill, width=1):
    """Draw a line from center outward at given angle."""
    x0 = cx + r_start * math.sin(angle)
    y0 = cy - r_start * math.cos(angle)
    x1 = cx + r_end * math.sin(angle)
    y1 = cy - r_end * math.cos(angle)
    draw.line([(x0, y0), (x1, y1)], fill=fill, width=width)


def _facing_dot(draw, cx, cy, angle, r, fill):
    """Draw a small dot indicating the facing direction relative to view angle."""
    x = cx + r * math.sin(angle)
    y = cy - r * math.cos(angle)
    draw.point([(int(x), int(y))], fill=fill)


# --- Unit drawing functions ---
# Each receives: draw, ox, oy (frame top-left), cx, cy (frame center), angle (view angle)
# angle=0 means viewing from the front; the "front-facing" features should be visible.
# As angle increases, we're rotating clockwise around the unit.

def draw_rifleman(draw, ox, oy, cx, cy, angle):
    # Green armored infantry with rifle
    body = (60, 140, 60)
    visor = (180, 220, 255)
    gun = (80, 80, 80)
    # Body
    _circle(draw, cx, cy + 1, 5, body)
    # Helmet
    _circle(draw, cx, cy - 3, 4, (40, 100, 40))
    # Visor (visible from front half)
    front = math.cos(angle)
    if front > 0.2:
        vx = cx + 2.5 * math.sin(angle)
        vy = cy - 3
        _rect(draw, int(vx) - 1, int(vy), int(vx) + 1, int(vy) + 1, visor)
    # Rifle - extends to the right side of view
    gun_angle = angle + 0.5
    _line_at_angle(draw, cx, cy, gun_angle, 3, 7, gun, 1)

def draw_heavy(draw, ox, oy, cx, cy, angle):
    # Bulky red heavy trooper
    body = (180, 40, 40)
    armor = (140, 30, 30)
    gun = (100, 100, 100)
    # Wide body
    _rect(draw, cx - 5, cy - 2, cx + 5, cy + 5, body)
    # Shoulder pads
    _rect(draw, cx - 6, cy - 3, cx - 3, cy - 1, armor)
    _rect(draw, cx + 3, cy - 3, cx + 6, cy - 1, armor)
    # Helmet
    _circle(draw, cx, cy - 4, 3, armor)
    # Visor
    front = math.cos(angle)
    if front > 0.1:
        vx = cx + 2 * math.sin(angle)
        _rect(draw, int(vx) - 1, cy - 5, int(vx) + 1, cy - 4, (255, 200, 50))
    # Big gun
    _line_at_angle(draw, cx, cy, angle + 0.3, 4, 8, gun, 2)

def draw_scout(draw, ox, oy, cx, cy, angle):
    # Slim blue scout
    body = (40, 100, 200)
    accent = (80, 180, 255)
    # Slim body
    _rect(draw, cx - 2, cy - 1, cx + 2, cy + 5, body)
    # Head
    _circle(draw, cx, cy - 3, 3, accent)
    # Antenna
    _line_at_angle(draw, cx, cy - 5, 0.3, 0, 3, (255, 255, 100), 1)
    # Speed lines (legs spread)
    lx = cx - 2 + 2 * math.sin(angle)
    rx = cx + 2 + 2 * math.sin(angle)
    draw.line([(lx, cy + 5), (lx - 1, cy + 7)], fill=body, width=1)
    draw.line([(rx, cy + 5), (rx + 1, cy + 7)], fill=body, width=1)
    # Eye
    front = math.cos(angle)
    if front > 0:
        ex = cx + 1.5 * math.sin(angle)
        draw.point([(int(ex), cy - 3)], fill=(255, 255, 255))

def draw_sniper(draw, ox, oy, cx, cy, angle):
    # Dark purple sniper with long barrel
    body = (80, 40, 120)
    cloak = (60, 30, 90)
    barrel = (60, 60, 70)
    # Cloak/body
    _rect(draw, cx - 3, cy - 2, cx + 3, cy + 5, cloak)
    # Hood
    _circle(draw, cx, cy - 3, 3, body)
    # Single glowing eye
    front = math.cos(angle)
    if front > 0:
        ex = cx + 1.5 * math.sin(angle)
        draw.point([(int(ex), cy - 3)], fill=(255, 50, 50))
    # Long sniper barrel
    _line_at_angle(draw, cx, cy - 1, angle, 3, 9, barrel, 1)
    # Scope glint
    sx = cx + 5 * math.sin(angle)
    sy = cy - 1 - 5 * math.cos(angle)
    draw.point([(int(sx), int(sy))], fill=(200, 200, 255))

def draw_medic(draw, ox, oy, cx, cy, angle):
    # White medic with red cross
    body = (220, 220, 230)
    cross = (220, 40, 40)
    # Body
    _circle(draw, cx, cy + 1, 5, body)
    # Helmet
    _circle(draw, cx, cy - 3, 3, body)
    # Red cross on chest (always visible from front)
    front = math.cos(angle)
    if front > -0.3:
        # Horizontal bar
        _rect(draw, cx - 2, cy, cx + 2, cy + 1, cross)
        # Vertical bar
        _rect(draw, cx, cy - 1, cx + 1, cy + 2, cross)
    else:
        # Back: small backpack
        _rect(draw, cx - 2, cy - 1, cx + 2, cy + 3, (180, 180, 190))
    # Visor
    if front > 0.2:
        vx = cx + 2 * math.sin(angle)
        _rect(draw, int(vx) - 1, cy - 4, int(vx) + 1, cy - 3, (100, 200, 255))

def draw_mech(draw, ox, oy, cx, cy, angle):
    # Boxy grey mech walker
    body = (120, 120, 130)
    dark = (80, 80, 90)
    eye = (255, 80, 80)
    # Boxy torso
    _rect(draw, cx - 4, cy - 4, cx + 4, cy + 2, body)
    # Cockpit window
    front = math.cos(angle)
    if front > 0:
        _rect(draw, cx - 2, cy - 3, cx + 2, cy - 1, dark)
        draw.point([(cx, cy - 2)], fill=eye)
    # Legs (wide stance)
    _rect(draw, cx - 5, cy + 2, cx - 3, cy + 7, dark)
    _rect(draw, cx + 3, cy + 2, cx + 5, cy + 7, dark)
    # Feet
    _rect(draw, cx - 6, cy + 6, cx - 2, cy + 7, body)
    _rect(draw, cx + 2, cy + 6, cx + 6, cy + 7, body)

def draw_drone(draw, ox, oy, cx, cy, angle):
    # Hovering disc drone with cyan glow
    body = (60, 70, 80)
    glow = (0, 220, 255)
    # Main disc body
    _circle(draw, cx, cy, 4, body)
    # Top dome
    _circle(draw, cx, cy - 1, 2, (80, 90, 100))
    # Glow ring underneath
    for i in range(8):
        a = i * math.pi / 4
        px = cx + 4 * math.cos(a)
        py = cy + 2 + 2 * math.sin(a)
        draw.point([(int(px), int(py))], fill=glow)
    # Eye/sensor
    front = math.cos(angle)
    if front > 0:
        ex = cx + 2 * math.sin(angle)
        draw.point([(int(ex), cy)], fill=glow)
    # Hover effect - pixels below
    for dx in [-2, 0, 2]:
        draw.point([(cx + dx, cy + 5)], fill=(0, 150, 180, 120))

def draw_flamethrower(draw, ox, oy, cx, cy, angle):
    # Orange flamethrower unit
    body = (200, 100, 30)
    tank = (140, 70, 20)
    flame = (255, 180, 0)
    # Body
    _circle(draw, cx, cy + 1, 5, body)
    # Helmet
    _circle(draw, cx, cy - 3, 3, (180, 80, 20))
    # Fuel tank on back
    back = -math.cos(angle)
    if back > 0:
        bx = cx - 3 * math.sin(angle)
        _rect(draw, int(bx) - 1, cy - 2, int(bx) + 1, cy + 3, tank)
    # Nozzle + flame
    _line_at_angle(draw, cx, cy, angle + 0.2, 3, 6, (100, 100, 100), 1)
    # Flame at end of nozzle
    fx = cx + 7 * math.sin(angle + 0.2)
    fy = cy - 7 * math.cos(angle + 0.2)
    _circle(draw, int(fx), int(fy), 2, flame)
    draw.point([(int(fx), int(fy))], fill=(255, 255, 100))
    # Visor
    front = math.cos(angle)
    if front > 0.2:
        vx = cx + 2 * math.sin(angle)
        draw.point([(int(vx), cy - 3)], fill=(255, 200, 50))

def draw_engineer(draw, ox, oy, cx, cy, angle):
    # Yellow engineer with tool
    body = (200, 180, 40)
    tool = (150, 150, 160)
    # Body
    _circle(draw, cx, cy + 1, 5, body)
    # Helmet with stripe
    _circle(draw, cx, cy - 3, 3, (180, 160, 30))
    _rect(draw, cx - 3, cy - 5, cx + 3, cy - 4, (40, 40, 40))
    # Tool/wrench
    _line_at_angle(draw, cx, cy + 1, angle - 0.5, 3, 6, tool, 1)
    tx = cx + 6 * math.sin(angle - 0.5)
    ty = cy + 1 - 6 * math.cos(angle - 0.5)
    draw.point([(int(tx), int(ty))], fill=(200, 200, 210))
    draw.point([(int(tx) + 1, int(ty))], fill=(200, 200, 210))
    # Visor
    front = math.cos(angle)
    if front > 0.2:
        vx = cx + 2 * math.sin(angle)
        _rect(draw, int(vx) - 1, cy - 4, int(vx), cy - 3, (100, 200, 255))

def draw_commander(draw, ox, oy, cx, cy, angle):
    # Gold commander with cape
    body = (180, 150, 50)
    cape = (160, 30, 30)
    gold = (220, 190, 60)
    # Cape (visible from back/sides)
    back = -math.cos(angle)
    if back > -0.3:
        bx = cx - 3 * math.sin(angle)
        _rect(draw, int(bx) - 2, cy - 1, int(bx) + 2, cy + 6, cape)
    # Body
    _circle(draw, cx, cy + 1, 5, body)
    # Helmet with crest
    _circle(draw, cx, cy - 3, 3, gold)
    _rect(draw, cx - 1, cy - 7, cx + 1, cy - 4, (255, 50, 50))  # crest plume
    # Visor
    front = math.cos(angle)
    if front > 0.2:
        vx = cx + 2 * math.sin(angle)
        _rect(draw, int(vx) - 1, cy - 4, int(vx) + 1, cy - 3, (200, 220, 255))
    # Shoulder insignia
    _rect(draw, cx - 5, cy - 2, cx - 4, cy - 1, gold)
    _rect(draw, cx + 4, cy - 2, cx + 5, cy - 1, gold)

def draw_artillery(draw, ox, oy, cx, cy, angle):
    # Large dark green artillery with cannon
    body = (50, 80, 50)
    barrel = (70, 70, 75)
    wheel = (60, 50, 40)
    # Base platform
    _rect(draw, cx - 5, cy + 1, cx + 5, cy + 4, body)
    # Wheels
    _circle(draw, cx - 4, cy + 5, 2, wheel)
    _circle(draw, cx + 4, cy + 5, 2, wheel)
    # Gun turret
    _circle(draw, cx, cy - 1, 3, (70, 100, 70))
    # Long cannon barrel (rotates with viewing angle)
    _line_at_angle(draw, cx, cy - 1, angle, 3, 9, barrel, 2)
    # Muzzle flash hint
    mx = cx + 9 * math.sin(angle)
    my = cy - 1 - 9 * math.cos(angle)
    draw.point([(int(mx), int(my))], fill=(255, 200, 100))


def draw_grenadier(draw, ox, oy, cx, cy, angle):
    # Teal grenadier with launcher tube
    body = (50, 140, 130)
    launcher = (90, 90, 95)
    # Body
    _circle(draw, cx, cy + 1, 5, body)
    # Helmet with face guard
    _circle(draw, cx, cy - 3, 3, (40, 110, 100))
    front = math.cos(angle)
    if front > 0.2:
        vx = cx + 2 * math.sin(angle)
        _rect(draw, int(vx) - 1, cy - 4, int(vx) + 1, cy - 2, (60, 60, 70))
    # Grenade launcher (thick tube)
    _line_at_angle(draw, cx, cy, angle + 0.4, 3, 7, launcher, 2)
    # Grenade at tip
    gx = cx + 7 * math.sin(angle + 0.4)
    gy = cy - 7 * math.cos(angle + 0.4)
    _circle(draw, int(gx), int(gy), 1, (200, 200, 50))

def draw_shield(draw, ox, oy, cx, cy, angle):
    # Dark blue shield trooper with large front shield
    body = (40, 50, 140)
    shield_c = (60, 80, 200)
    # Body
    _circle(draw, cx, cy + 1, 4, body)
    # Helmet
    _circle(draw, cx, cy - 3, 3, (30, 40, 110))
    front = math.cos(angle)
    # Shield in front
    if front > -0.2:
        sx = cx + 4 * math.sin(angle)
        _rect(draw, int(sx) - 3, cy - 4, int(sx) + 1, cy + 4, shield_c)
        # Shield highlight
        _rect(draw, int(sx) - 2, cy - 3, int(sx), cy + 3, (80, 110, 230))
    # Visor slit
    if front > 0.3:
        vx = cx + 2 * math.sin(angle)
        draw.point([(int(vx), cy - 3)], fill=(200, 220, 255))
    # Short sidearm
    _line_at_angle(draw, cx, cy, angle - 0.6, 3, 5, (80, 80, 80), 1)

def draw_jetpack(draw, ox, oy, cx, cy, angle):
    # Light grey jetpack trooper with thruster glow
    body = (160, 160, 170)
    jet = (255, 120, 30)
    # Body (slightly elevated)
    _circle(draw, cx, cy - 1, 4, body)
    # Helmet with visor
    _circle(draw, cx, cy - 4, 3, (140, 140, 150))
    front = math.cos(angle)
    if front > 0.2:
        vx = cx + 2 * math.sin(angle)
        _rect(draw, int(vx) - 1, cy - 5, int(vx) + 1, cy - 4, (0, 200, 255))
    # Jetpack on back
    back = -math.cos(angle)
    if back > -0.5:
        bx = cx - 3 * math.sin(angle)
        _rect(draw, int(bx) - 1, cy - 3, int(bx) + 1, cy + 1, (100, 100, 110))
    # Thruster flames below
    for dx in [-1, 1]:
        fx = cx + dx - 1.5 * math.sin(angle)
        _rect(draw, int(fx), cy + 3, int(fx) + 1, cy + 6, jet)
        draw.point([(int(fx), cy + 6)], fill=(255, 255, 100))
    # Pistol
    _line_at_angle(draw, cx, cy - 1, angle + 0.5, 3, 5, (80, 80, 80), 1)

def draw_hacker(draw, ox, oy, cx, cy, angle):
    # Dark teal EW/hacker with antenna array
    body = (30, 80, 80)
    screen = (0, 255, 120)
    # Body
    _rect(draw, cx - 3, cy - 1, cx + 3, cy + 5, body)
    # Hooded head
    _circle(draw, cx, cy - 3, 3, (20, 60, 60))
    # Screen glow on front
    front = math.cos(angle)
    if front > 0:
        _rect(draw, cx - 2, cy, cx + 2, cy + 2, (10, 40, 40))
        draw.point([(cx - 1, cy + 1)], fill=screen)
        draw.point([(cx + 1, cy + 1)], fill=screen)
        draw.point([(cx, cy)], fill=screen)
    # Antenna array on top
    for dx in [-2, 0, 2]:
        draw.line([(cx + dx, cy - 5), (cx + dx, cy - 7)], fill=(0, 180, 100), width=1)
    # Glowing eyes
    if front > 0.1:
        ex = cx + 1.5 * math.sin(angle)
        draw.point([(int(ex), cy - 3)], fill=screen)
        draw.point([(int(ex) + 1, cy - 3)], fill=screen)

def draw_berserker(draw, ox, oy, cx, cy, angle):
    # Crimson melee berserker with energy blade
    body = (160, 30, 50)
    blade = (255, 60, 60)
    # Bulky body
    _circle(draw, cx, cy + 1, 5, body)
    _rect(draw, cx - 5, cy - 1, cx + 5, cy + 3, (140, 25, 40))
    # Spiked helmet
    _circle(draw, cx, cy - 3, 3, (120, 20, 30))
    _rect(draw, cx - 1, cy - 7, cx, cy - 4, body)  # center spike
    _rect(draw, cx - 3, cy - 6, cx - 2, cy - 4, body)  # left spike
    _rect(draw, cx + 2, cy - 6, cx + 3, cy - 4, body)  # right spike
    # Glowing eyes
    front = math.cos(angle)
    if front > 0:
        ex = cx + 1.5 * math.sin(angle)
        draw.point([(int(ex), cy - 3)], fill=(255, 200, 0))
        draw.point([(int(ex) + 1, cy - 3)], fill=(255, 200, 0))
    # Energy blade (swung to the side)
    _line_at_angle(draw, cx, cy, angle + 0.8, 4, 9, blade, 1)
    bx = cx + 9 * math.sin(angle + 0.8)
    by = cy - 9 * math.cos(angle + 0.8)
    draw.point([(int(bx), int(by))], fill=(255, 200, 200))

def draw_stealth(draw, ox, oy, cx, cy, angle):
    # Semi-transparent stealth unit, dark with shimmer
    body = (40, 40, 50, 180)
    outline = (80, 80, 100, 200)
    eye = (0, 255, 200)
    # Slim body outline
    _rect(draw, cx - 2, cy - 1, cx + 2, cy + 5, body)
    # Head
    _circle(draw, cx, cy - 3, 2, body)
    # Outline effect
    for dy in range(-4, 6):
        draw.point([(cx - 3, cy + dy)], fill=outline)
        draw.point([(cx + 3, cy + dy)], fill=outline)
    # Single glowing eye
    front = math.cos(angle)
    if front > 0:
        ex = cx + 1 * math.sin(angle)
        draw.point([(int(ex), cy - 3)], fill=eye)
    # Dagger
    _line_at_angle(draw, cx, cy + 1, angle + 0.3, 2, 5, (120, 120, 130), 1)

def draw_tank(draw, ox, oy, cx, cy, angle):
    # Small tracked tank, olive with turret
    hull = (80, 90, 50)
    turret = (100, 110, 60)
    track = (50, 50, 40)
    barrel = (70, 70, 75)
    # Tracks
    _rect(draw, cx - 6, cy + 2, cx - 4, cy + 6, track)
    _rect(draw, cx + 4, cy + 2, cx + 6, cy + 6, track)
    # Hull
    _rect(draw, cx - 5, cy + 1, cx + 5, cy + 5, hull)
    # Turret
    _circle(draw, cx, cy, 3, turret)
    # Barrel
    _line_at_angle(draw, cx, cy, angle, 3, 8, barrel, 2)
    # Hatch detail
    _circle(draw, cx, cy - 1, 1, (120, 130, 70))

def draw_turret_unit(draw, ox, oy, cx, cy, angle):
    # Deployable turret — tripod base with gun
    base = (100, 100, 90)
    gun = (70, 70, 75)
    # Tripod legs
    for a_off in [0, 2.1, 4.2]:
        lx = cx + 4 * math.cos(a_off)
        ly = cy + 3 + 3 * math.sin(a_off)
        draw.line([(cx, cy + 2), (int(lx), int(ly))], fill=(80, 80, 70), width=1)
    # Base platform
    _circle(draw, cx, cy + 1, 3, base)
    # Gun barrel (rotates)
    _line_at_angle(draw, cx, cy, angle, 2, 7, gun, 2)
    # Ammo box
    _rect(draw, cx - 2, cy + 2, cx + 2, cy + 4, (90, 80, 50))
    # Muzzle flash
    mx = cx + 7 * math.sin(angle)
    my = cy - 7 * math.cos(angle)
    draw.point([(int(mx), int(my))], fill=(255, 255, 150))

def draw_psychic(draw, ox, oy, cx, cy, angle):
    # Purple/pink psychic unit with energy aura
    body = (120, 50, 140)
    aura = (180, 80, 220, 140)
    energy = (220, 150, 255)
    # Aura ring
    for i in range(12):
        a = i * math.pi / 6
        px = cx + 6 * math.cos(a)
        py = cy + 6 * math.sin(a)
        draw.point([(int(px), int(py))], fill=aura)
    # Robed body
    _rect(draw, cx - 3, cy - 1, cx + 3, cy + 6, body)
    # Wider base (robe)
    _rect(draw, cx - 4, cy + 4, cx + 4, cy + 6, (100, 40, 120))
    # Hooded head
    _circle(draw, cx, cy - 3, 3, (100, 40, 120))
    # Glowing eyes
    front = math.cos(angle)
    if front > 0:
        ex = cx + 1.5 * math.sin(angle)
        draw.point([(int(ex), cy - 3)], fill=energy)
        draw.point([(int(ex) + 1, cy - 3)], fill=energy)
    # Energy orb floating above hand
    orb_x = cx + 4 * math.sin(angle - 0.5)
    orb_y = cy - 2 - 4 * math.cos(angle - 0.5)
    _circle(draw, int(orb_x), int(orb_y), 1, energy)


UNITS = {
    "rifleman": draw_rifleman,
    "heavy": draw_heavy,
    "scout": draw_scout,
    "sniper": draw_sniper,
    "medic": draw_medic,
    "mech": draw_mech,
    "drone": draw_drone,
    "flamethrower": draw_flamethrower,
    "engineer": draw_engineer,
    "commander": draw_commander,
    "artillery": draw_artillery,
    "grenadier": draw_grenadier,
    "shield": draw_shield,
    "jetpack": draw_jetpack,
    "hacker": draw_hacker,
    "berserker": draw_berserker,
    "stealth": draw_stealth,
    "tank": draw_tank,
    "turret": draw_turret_unit,
    "psychic": draw_psychic,
}


def generate_sprite(name):
    """Generate a sprite sheet PNG and INI for the given unit type."""
    cols = 1  # just idle for now
    img = Image.new("RGBA", (S * cols, S * ANGLES), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    for angle_idx in range(ANGLES):
        ox = 0
        oy = angle_idx * S
        draw_unit(draw, ox, oy, angle_idx, name)

    png_path = f"{name}.png"
    ini_path = f"{name}.ini"

    img.save(png_path)
    print(f"  wrote {png_path} ({img.width}x{img.height})")

    with open(ini_path, "w") as f:
        f.write(f"frame_width={S}\n")
        f.write(f"frame_height={S}\n")
        f.write(f"angles={ANGLES}\n")
        f.write(f"fps=4\n")
        f.write(f"\n")
        f.write(f"[idle]\n")
        f.write(f"frames=0\n")

    print(f"  wrote {ini_path}")


def main():
    print(f"Generating {len(UNITS)} sprite sheets ({S}x{S}, {ANGLES} angles)...")
    for name in UNITS:
        print(f"\n{name}:")
        generate_sprite(name)
    print("\nDone!")


if __name__ == "__main__":
    main()
