# sphere-voxel — resume context

**Status:** seeded, not started. No code exists yet.

**Brainstormed:** 2026-04-08. This idea emerged during a conversation where the user asked whether `libs/raytrace` could render a level made from simple primitives (originally "rectangles," then clarified to spheres). The sibling `sphere-bricks` idea captures the simpler freeform-placement version; this one is the grid-accelerated variant.

## What's been decided

- **New renderer lib, new pattern.** A sibling to `libs/raytrace` that implements a uniform 3D grid + DDA traversal + per-cell sphere intersection.
- **Reuses `libs/raytrace`'s sphere intersection math directly** — do not re-implement it. The new work is the grid data structure and the 3D DDA walker, not the primitive intersection.
- **This is a fourth category of software renderer** — not a pure raycaster (it renders true 3D, not 2D grid worlds), not a rasterizer, not a general raytracer. The single-primitive constraint is what enables both the simpler hot loop and the cheaper acceleration structure.
- **Perf viability confirmed by back-of-envelope math**: ~10M ops/frame at 320×240 with realistic DDA depths and sphere counts, which is single-threaded 50 fps on a 500 MHz CPU. Viable on Raspberry Pi Zero 2 / Pentium II / similar.
- **Engine layer mirrors battleforge** — commands, ECS, picking, level loading.
- **Related seeds**:
  - `sphere-bricks` — same primitive, no grid, no acceleration, freeform placement. Simpler, caps at small scene counts.
  - `raycast-grid` — 2D grid Wolf3D-style raycaster. Different technique.

## What's still open

- **Name for the renderer lib.** Candidates: `libs/orbworld`, `libs/spherevoxel`, `libs/sphgrid`. Not chosen.
- **Grid representation**: dense 3D array (simple, memory-hungry) vs sparse (chunks or hash-based, more complex). Probably start dense.
- **Cell vs sphere geometry**: does each cell contain exactly one sphere at its center, or can spheres straddle cell boundaries? The latter complicates DDA (a sphere in cell A might be hit by a ray traversing cell B). Simplest working choice: one sphere per cell, fits within the cell, no straddling.
- **Sphere per-cell attributes**: just color + radius, or also material properties, emissive, transparency?
- **Game concept.** What kind of game? Platformer, puzzle, exploration, something else. Not chosen.
- **Authoring tooling.** File format for voxel grids — image stack (each PNG slice is a layer), custom binary, text? Or procedural generation from noise?
- **Whether spheres can be animated / moved.** Static-world-plus-dynamic-entities is easier than everything-is-dynamic. Probably start with static world + separate entity list rendered similarly.

## When resuming

1. Re-read this file and `README.md`.
2. Look at `libs/raytrace/sphere.c` — the intersection code you'll be reusing.
3. Look at `apps/rtdemo/main.c` — the minimal existing raytrace client, good reference shape.
4. **First milestone**: a standalone program that renders a 3D grid of spheres using 3D DDA. No engine layer yet, no entities, no input beyond camera rotation. Just: hardcoded small grid (say 16×16×16 with some pattern), 3D DDA walker, sphere intersection per filled cell, simple Lambert shading. Benchmark it at 320×240 and 640×480. This single prototype validates or invalidates the whole idea.
5. If the benchmark is good, start building the engine layer on top, following `libs/battleforge` as the pattern.
6. Reference reading for 3D DDA: Amanatides & Woo, "A Fast Voxel Traversal Algorithm for Ray Tracing" (1987). Short paper, the canonical reference.

## Distinction from sphere-bricks

Keep the two projects separate. `sphere-bricks` is a freeform-placement exercise using `libs/raytrace` unchanged; it has no grid, no acceleration, and caps at small scene counts. `sphere-voxel` is grid-locked with genuine acceleration and can scale to tens of thousands of cells — but requires new renderer code (the 3D DDA walker + grid data structure). They produce different aesthetics and solve different engineering problems.

## Related seeds

- `../sphere-bricks/` — the freeform placement variant.
- `../raycast-grid/` — 2D grid raycaster (Wolf3D-style).
- `../raster-software/` — software triangle rasterizer (MDK-style, much larger).
