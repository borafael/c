# sphere-voxel

A **voxel-style world where each voxel is a sphere**, rendered with a uniform 3D grid + DDA traversal + sphere intersection. A hybrid: the data structure of a raycaster (uniform grid, DDA stepping) combined with the rendering quality of a raytracer (true 3D, free per-pixel lighting, look any direction).

This is a fourth category of software renderer — neither pure raycaster nor rasterizer nor general raytracer. It exists because a single-primitive constraint (everything is a sphere) enables both a simpler inner loop and a cheaper acceleration structure than any general-purpose renderer could use.

## The core idea

Imagine a Minecraft-style voxel grid, but each filled cell contains a **sphere** instead of a cube. Rendering:

```
for each pixel on screen:
    cast a ray from camera through pixel
    walk cells in 3D using DDA (Wolfenstein-style, but 3D)
    if a cell has a sphere:
        test ray-sphere intersection
        if hit: shade and stop
```

Why this is fast:

- **DDA traversal is cheap.** Stepping through grid cells takes a handful of integer ops per step. Most cells are empty and cost nothing.
- **Only one primitive per cell** means when you do hit a non-empty cell, the intersection test is a single ~10-FLOP sphere test, not a BVH traversal or primitive dispatch.
- **Early termination.** The moment any ray hits any sphere, you stop walking — so average ray cost is bounded by the depth to the first hit, not the scene size.
- **Uniform inner loop.** The renderer has no type dispatch, no conditional branches on primitive kind. The compiler can inline everything and SIMD-vectorize trivially.

Rough performance estimate at 320×240 with ~15 DDA steps + ~3 sphere tests per ray and modest shading: **~10M ops/frame**. On a 500 MHz CPU that's ~20 ms/frame = 50 fps single-threaded. Viable on a Raspberry Pi Zero 2, Pentium II, or similar.

## Target aesthetic and gameplay feel

- Marble / orb / soap-bubble worlds
- Distinctive visual identity — very few games look like this
- Lighting is trivially per-pixel correct because sphere normals are closed-form (`(hit - center) / radius`)
- Free 6-DOF camera, look any direction, jump, climb
- Voxel placement authoring — Minecraft-style or image-stack loading

## Planned structure

```
libs/<sphere-voxel-lib>     new renderer lib: 3D uniform grid + DDA + sphere intersection
libs/<engine>               engine layered on the renderer
apps/<client>               SDL2 client
```

The renderer lib is new, but it **reuses `libs/raytrace`'s sphere intersection code directly** — it's not duplicated. The new code is the 3D DDA traversal and the grid data structure.

## Relationship to sibling seeds

- **`sphere-bricks`** is the freeform placement variant: a flat list of spheres, no grid, no DDA, no acceleration. Simpler to build, works with raytrace unchanged, but caps out at a few hundred spheres for performance. Artistic / Lego feel.
- **`sphere-voxel`** (this one) is grid-locked: spheres snap to cell positions, which enables massive scenes (tens of thousands of cells) via cheap DDA traversal. Voxel / Minecraft feel.

Different projects, different aesthetics, different performance ceilings.

## What it would reuse

- `libs/raytrace` — specifically `sphere.c`'s intersection math. The rest of raytrace is not used.
- `libs/math` — vectors and basic operations
- `libs/ds` — probably for a sparse grid representation or entity lists
- `libs/ini` — level config (lighting, sky color, fog)
- `libs/thread` — parallelize across scanlines or tiles

## What's new vs existing code

The genuinely new work:

1. **3D DDA traversal**: extend the standard 2D DDA grid walker to three dimensions. Well-known algorithm (Amanatides & Woo, 1987).
2. **Uniform grid data structure**: a 3D array of cells, each optionally holding a sphere. Probably start dense, move to sparse representation later if memory pressure demands it.
3. **Per-cell sphere intersection**: trivial given the existing `rt_sphere` math.
4. **Shading**: Lambert at minimum, point lights optional. All closed-form from sphere normals.

Everything else (engine layer, client, scene loading) is the same pattern as battleforge/barrier.

## Why this is interesting

- **It's genuinely novel.** Sphere-voxel worlds are rare. You'd be exploring unexamined visual territory.
- **Reuses `libs/raytrace` meaningfully** — exercises its sphere primitive in a new context.
- **Low-spec viable** — the 3D DDA + single primitive combination is cache-friendly and SIMD-friendly.
- **Authoring is intuitive.** Voxel placement is a well-understood UX.
- **Physics is almost free** — sphere-sphere collision is the simplest test in all of rigid-body dynamics.
