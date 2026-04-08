# sphere-bricks — resume context

**Status:** seeded, not started. No code exists yet.

**Brainstormed:** 2026-04-08, during a conversation about what kind of renderer a new game engine should use. The user asked whether `libs/raytrace` could serve as the renderer directly if the world were built out of simple primitives — and then specifically clarified they meant spheres, not boxes.

## What's been decided

- **No new renderer lib.** `libs/raytrace` is used **unchanged**. The entire project is the engine layer + client + scene design.
- **Scenes contain only spheres.** This is a self-imposed discipline, not a technical limitation of raytrace (which also supports boxes, planes, triangles, etc.). The constraint is creative: every object in the world is a sphere.
- **No acceleration structure.** Scenes are a flat list of spheres. `libs/raytrace` currently has no BVH or grid, so per-ray cost scales linearly with sphere count. Target scene sizes of a few hundred spheres are fine; beyond ~1000 the project converges on `sphere-voxel` territory.
- **Engine layer mirrors battleforge** — commands, ECS, picking, level loading — but is simpler because there's no terrain heightmap to handle. The "map" is literally a list of sphere positions.
- **Related seed is `sphere-voxel`**, which is the grid-accelerated variant. Keep them separate: `sphere-bricks` is freeform placement (Lego feel), `sphere-voxel` is grid-locked (voxel feel).

## What's still open

- **Game concept.** What kind of game is built from sphere-only worlds? Candidates discussed briefly: marble-roller, platformer, puzzle, exploration, orb-based shooter. None chosen.
- **Scene authoring.** Text file listing spheres? INI-based? In-game placement editor? A procedural generator? Any of these are viable; none chosen.
- **Scene size ceiling.** How many spheres can be rendered at target framerate on target hardware before things slow down? Needs measurement; will inform whether a BVH is eventually added or whether the project stays small.
- **Whether to ever add a BVH to libs/raytrace.** If scenes push past what flat iteration can handle, adding a BVH to raytrace would benefit this project (and others). But that's optional and deferred.
- **Names** for the engine lib and client app.

## When resuming

1. Re-read this file and `README.md`.
2. Look at `apps/rtdemo/main.c` — it's the existing minimal raytrace client and is the fastest starting point. Copy-modify that to place, say, 30-50 spheres in some arrangement (a ring, a pyramid, a simple figure). See what it looks like. Measure FPS at 320x240 and 640x480.
3. That prototype alone might be enough to know whether the aesthetic works. If yes, start building the engine layer on top, following `libs/battleforge` as the pattern.
4. Don't pre-optimize. The flat-iteration raytrace is fine until it isn't. Measure first.

## A note on the pivot during brainstorming

This idea came up when the user asked "could we use our current raytracer as a renderer? if we for example use rectangles to define a level?" — and then clarified: spheres, not rectangles. A sibling "sphere-voxel" idea emerged during the conversation as a grid-accelerated variant, but this seed specifically captures the **simpler** version: use raytrace as-is, constrain yourself to spheres, place them freeform.

## Related seeds

- `../sphere-voxel/` — the grid-accelerated variant. Same primitive, different data structure and performance characteristics.
- `../raycast-grid/` — Wolf3D-style raycaster. Different technique, harder low-spec target.
- `../raster-software/` — software triangle rasterizer. Much larger project.
