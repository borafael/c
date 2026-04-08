# voxel-space — resume context

**Status:** seeded, not started. No code exists yet.

**Brainstormed:** 2026-04-08. This idea was mentioned briefly during the initial rendering-technique discussion (as "Comanche-style voxel/heightmap raycaster"), then seeded as a standalone concept after the user asked about it by name.

## What's been decided

- **Technique**: per-column front-to-back heightmap marching with painter's-algorithm occlusion (a 1D "frontmost Y per column" mask). No ray-primitive intersection. No z-buffer. Inner loop is a heightmap sample + colormap sample + projection divide + pixel write.
- **World format**: two 2D bitmaps — a grayscale heightmap (elevation per ground point) and an RGB colormap (color per ground point). Authored in any image editor.
- **New renderer lib**: `libs/voxspace` (name tentative), sibling to `libs/raytrace`. Small — possibly a few hundred lines for the core marcher.
- **Engine layer**: mirrors the battleforge pattern (commands, ECS, entity list, picking, level loading).
- **Reuses**: `libs/math`, `libs/ds`, `libs/ini`, `libs/slice`, `libs/thread`, and the same `stb_image`-based PNG loading path `apps/barrier` uses.
- **Crossover with `libs/raytrace/heightfield.c`**: important. That file already does heightfield intersection, used by battleforge/barrier for terrain. This project renders **the same kind of data** (PNG heightmaps) but with a fundamentally faster technique. A natural A/B comparison point with `barrier`.
- **Lowest-spec of the five seeded ideas**: historically shipped on a 386 in 1992. On modern weak hardware it's trivially 60+ fps at 640×480 single-threaded.

## What's still open

- **Camera controls**: fully free 6DOF (fly anywhere, look up/down clamped) or constrained (helicopter-sim style with altitude limits)? Different gameplay implications.
- **Game concept**: vibe candidates include helicopter sim, tank combat, rover exploration, alien planet walker, desert driving. None chosen.
- **Colormap source**: authored RGB image, or generated from heightmap (color-by-elevation like `bf_map_generate_test_terrain` does), or both? Probably support both.
- **Entity rendering**: billboarded sprites over the terrain, but how to handle occlusion against terrain? Simplest: project sprite center to screen, check column's `frontmost_y` to see if it's visible. Adequate for small entities; breaks for tall ones.
- **Distance fog**: cheap and visually essential — fades colors toward sky color as `z → zfar`. Should be standard.
- **Pitch clamping**: how aggressively. The projection degrades at extreme pitch. Clamp at ~±60° as a starting point.
- **Scale of one heightmap pixel**: a meter? A centimeter? Affects camera movement speed and what "large" means.
- **Names**: for the engine lib, the renderer lib (voxspace is tentative), and the client app.

## When resuming

1. Re-read this file and `README.md`.
2. Look at `libs/raytrace/heightfield.c` for how raytrace consumes the same kind of data today, and `apps/barrier/main.c` (lines that load heightmap PNGs) for the existing PNG-loading path you can reuse.
3. **First milestone — standalone marcher, no engine, no entities.** Write a minimal SDL2 program that:
   - Loads a heightmap PNG + colormap PNG
   - Has a camera (position, yaw, pitch)
   - Renders the terrain via column marching
   - Handles WASD + mouse look
   That single prototype will validate the technique and feel. Should be ~400 lines total.
4. Only after the prototype looks and feels right, start building the engine layer. Follow `libs/battleforge` as the pattern.
5. A natural "stretch goal 1" is loading one of `apps/barrier/maps/*.ini` heightmaps to A/B test against battleforge rendering the same terrain. Direct visual + perf comparison.

## Reference reading if stuck

- The technique has been written up many times; "Comanche Voxel Space engine" or "voxel terrain engine" will find explanations.
- The original NovaLogic engineer's write-ups of Voxel Space are on the internet — short and clear.
- Sebastian Macke's "voxelspace" project on GitHub is a well-known, tiny, legible reimplementation in multiple languages. Good reference for the minimal algorithm.

## Related seeds

- `../raycast-grid/` — 2D grid raycaster. Indoor/grid-world equivalent; can't do open terrain.
- `../raster-software/` — software triangle rasterizer. Can render arbitrary terrain but much slower and much bigger project.
- `../sphere-bricks/` and `../sphere-voxel/` — sphere-primitive worlds using raytrace. Different aesthetic entirely.

## Relationship to the existing `libs/raytrace/heightfield.c`

Worth being explicit: these are not the same technique. `raytrace/heightfield.c` does **ray-vs-heightfield intersection per pixel** — given an arbitrary ray, it finds where that ray hits the terrain surface. That's flexible (handles any camera ray, including reflected/refracted ones) but pays per-pixel intersection cost. `voxel-space` only handles **primary camera rays from a non-rolled camera, front-to-back**, in exchange for near-zero per-pixel cost. The two renderers could in principle consume the same heightmap data, but they're solving different problems and neither subsumes the other.
