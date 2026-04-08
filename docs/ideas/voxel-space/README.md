# voxel-space

A **per-column heightmap marcher** in the style of NovaLogic's *Voxel Space* engine (Comanche: Maximum Overkill, 1992). Renders vast outdoor terrain at playable speed on truly weak hardware by skipping intersection math entirely — the inner loop is a texture lookup and a pixel write.

Confusingly, this is neither a raycaster (no ray-vs-primitive intersection), nor a raytracer, nor a rasterizer, nor a voxel engine in the modern Minecraft sense. The name "Voxel Space" predates modern usage; the technique is closer to a **direct heightmap march with painter's algorithm**.

## The core idea

The world is two 2D bitmaps stacked in memory:

- **Heightmap** — grayscale image, each pixel's brightness is the terrain elevation at that point
- **Colormap** — RGB image, each pixel is the terrain color at that point

That's the entire world representation. No geometry. No triangles. No grid cells. Authoring is literally "draw two PNGs."

Rendering marches each screen column from near to far across the 2D ground plane, sampling the heightmap + colormap at each step:

```
for each screen column x:
    compute ray direction on ground plane (from camera yaw + column x)
    frontmost_y = screen_height   // bottom of column = "nothing drawn yet"
    for distance z from znear to zfar (step outward):
        sample_x = camera.x + ray_dx * z
        sample_z = camera.z + ray_dz * z
        terrain_height = heightmap[sample_x, sample_z]
        projected_y = project(terrain_height, z, camera.pitch, camera.y)
        if projected_y < frontmost_y:
            color = colormap[sample_x, sample_z]
            draw vertical line from projected_y to frontmost_y with color
            frontmost_y = projected_y
    fill [0, frontmost_y) with sky color
```

The key trick: you march **front to back**, tracking the top pixel already drawn in each column. Each new terrain sample either contributes nothing (if it's hidden behind closer terrain) or extends the column upward. No z-buffer needed — the "frontmost_y per column" is a perfect 1D occlusion mask because you're always processing nearer samples first.

## Target aesthetic and gameplay feel

- **Vast outdoor terrain** — hills, valleys, mountain ranges. Horizon visible.
- Vibes: helicopter sim, flight sim, tank combat on open landscapes, first-person exploration of alien planets, spaghetti-western desert, mars rover
- Camera flies freely above the terrain with pitch / yaw / height control
- Sprites for entities (trees, enemies, vehicles) — billboarded on top of the terrain, same approach as all the other seeds
- **1992-era vibe**: the technique gives a characteristic "horizontal streaking" look at grazing angles, which is part of its identity

## Target performance

The technique is **genuinely absurdly fast** because:

- Per-pixel cost is a texture lookup + a projection divide + a pixel write
- No intersection math, no ray-vs-anything tests, no BVH traversal
- Most pixels are skipped (occluded by closer terrain = zero work)
- Inner loop is branchless and SIMD-friendly

NovaLogic shipped this on a **386 in 1992**. On any modern low-spec target (Raspberry Pi Zero, cheap SBCs, old netbooks) it's trivial to hit 60+ fps at 640×480 single-threaded.

## Planned structure

```
libs/voxspace       per-column heightmap marcher (new)
libs/<engine>       engine layered on voxspace
apps/<client>       SDL2 client
```

The renderer lib is new but small — the whole marcher might be a few hundred lines. The engine layer above mirrors the battleforge pattern (commands, ECS, entity list, picking, level loading).

## Interesting crossover with existing code

`libs/raytrace/heightfield.c` already exists, and `libs/battleforge` uses it for terrain (see `apps/barrier` with PNG heightmaps in `apps/barrier/maps/`). But raytrace renders heightfields **per-pixel via ray-vs-heightfield intersection**, which is the slow way.

**voxel-space would render exactly the same kind of data, but vastly faster**, by exploiting the heightmap's structure directly instead of treating it as a generic 3D primitive.

Concretely: the PNG heightmaps that `apps/barrier` loads could potentially be consumed by a voxel-space renderer with minimal transformation. Same world data, different (much faster) renderer. This creates a natural comparison point — you could A/B test the two renderers on the same level and see the perf difference directly.

## What it cannot do

Be honest about limitations:

- **Outdoor terrain only.** No walls, no interiors, no overhangs, no caves, no bridges, no tunnels. Every point in the world is described by a single height-above-the-ground — the data format doesn't permit anything else.
- **No second-surface features.** If you want a river under a bridge, or a tunnel through a mountain, this renderer cannot represent it. You'd need to fake it with sprites or give up.
- **No true 3D objects.** Everything that's "on" the terrain (vehicles, trees, enemies) has to be a billboard sprite. Same limitation as Wolfenstein, for the same reason.
- **Distortion at steep pitches.** Looking straight down at the ground breaks the projection. Typically you clamp pitch to avoid this.

## What it would reuse

- `libs/math` — camera math, ground-plane rays
- `libs/ds` — entity lists
- `libs/ini` — level config (sky color, fog distance, light direction)
- `libs/slice` — sprite sheets for entities (helicopters, tanks, trees)
- `libs/thread` — trivial per-column parallelism
- **Heightmap PNG loading** — the same code path `apps/barrier` uses via `stb_image`

## Why this is a strong low-spec candidate

Of the five seeded ideas, `voxel-space` might actually be the **fastest on the weakest hardware**:

- `raycast-grid` is fast but still does DDA + texture mapping per column
- `sphere-bricks` and `sphere-voxel` pay raytracing's per-pixel costs
- `raster-software` is the slowest per-pixel and the largest project
- **`voxel-space` is pure memory reads + integer math**, with most pixels skipped entirely

It's genuinely the strongest match for the original "really low spec" goal.
