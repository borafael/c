# raycast-grid

A Wolfenstein 3D-style grid raycaster engine, designed with special emphasis on running playably on **really low-spec machines**. The technical constraint (maximum speed on minimum hardware) is the defining feature of the project.

## The core idea

A raycaster is a rendering technique that trades dimensionality for speed. The world is a 2D grid of cells — walls are either present or absent in each cell — and the renderer casts **one ray per screen column** (not per pixel), using DDA grid traversal to find the first wall hit. Each column is then drawn as a single textured vertical slice, scaled by the hit distance.

This constraint (2D grid only) is what makes raycasters vastly faster than raytracers or rasterizers in software: for a 640-column render you do ~640 rays per frame instead of ~300,000. Wolfenstein 3D ran on a 286 in 1992 for exactly this reason.

## Target aesthetic and gameplay feel

- Retro FPS — blocky, textured walls, billboard sprites for enemies/items
- 90° camera, grid-aligned movement (or smooth movement over a grid)
- Flat or single-height worlds to start; variable heights as a possible growth path
- "Looks like 1992, runs on 2024's cheapest hardware"

## Planned structure

Following the pattern already established by `libs/raytrace` + `libs/battleforge` + `apps/barrier`:

```
libs/raycast        grid renderer (DDA + column drawer + sprite compositor)
libs/<engine>       game engine layered on raycast (ECS, commands, picking, level loading)
apps/<client>       SDL2 client
```

The engine layer would mirror `battleforge` closely — commands, ECS components, entity picking, INI-based level loading. The renderer layer would be new code sibling to `libs/raytrace`.

## What it would reuse from existing libs

- `math` — vectors and basic math
- `slice` — sprite sheets with angle frames (perfect for enemies seen from multiple sides)
- `ini` — level config files, following the pattern `apps/barrier/maps/*.ini` uses
- `ds` — dynamic arrays for entities and sprite lists
- `thread` — optional, for parallel column rendering (raycasters parallelize trivially per column)

Nothing would be reused from `libs/raytrace`: this is a fundamentally different rendering technique, not a variation of raytracing.

## Growth path

Start minimal (Wolf3D-style — flat grid, textured walls, billboard sprites, solid floor/ceiling). Design the renderer's internal seams so the following can be added later without a rewrite:

- Textured floors and ceilings (new pass, doesn't touch wall code)
- Y-shearing for fake "look up/down"
- Variable wall heights per cell (new column drawer, new map cell field)
- Variable eye height / jumping

What this project deliberately does **not** promise: BSP (Doom-style), portal-walking (Build-style), arbitrary non-grid geometry. Those are different engines and would be separate projects.

## Why this is distinctive in the repo

It would be the first project in this monorepo that genuinely targets maximally weak hardware. Every other app so far accepts the cost of general-purpose software rendering (`libs/raytrace`); raycast-grid would trade flexibility for raw speed, and the constraint itself is the creative point.
