# raycast-grid — resume context

**Status:** seeded, not started. No code exists yet.

**Brainstormed:** 2026-04-08, in conversation about building a raycaster engine with emphasis on low-spec playability.

## What's been decided

- **Architecture**: follows the `libs/raytrace` + `libs/battleforge` + `apps/barrier` pattern — a new `libs/raycast` (renderer) + new engine lib + new SDL2 client app.
- **Rendering technique**: Wolfenstein 3D-style grid raycaster. DDA traversal per column, textured vertical slices, billboard sprites for entities.
- **Growth strategy**: ship a minimal Wolf3D feature set first, but design the renderer's internal seams to accept additive features later (textured floors/ceilings, y-shearing, variable wall heights). Explicitly **not** in scope: BSP, Build-style portal rendering, arbitrary non-grid geometry.
- **Design seams to commit to from day one**:
  - Map cell is a `struct` (not a byte), so new fields like `floor_z`/`ceiling_z`/`height` can be added later.
  - DDA result is a generic "ray hit" struct, separate from the column-drawing function. Swapping drawers doesn't touch traversal.
  - Renderer signature mirrors `rt_render_chunk(pixel_buf, viewport, y_start, y_end, camera, scene)` — chunkable for parallelism.
  - Entity positions are **world-space floats**, not grid indices. This keeps the engine API renderer-agnostic (like battleforge already does with `vector`).
- **Reuses**: `libs/math`, `libs/slice`, `libs/ini`, `libs/ds`, optionally `libs/thread`. Does **not** reuse `libs/raytrace` — different technique.

## What's still open

- **Performance target specifics**: how low is "low spec"? 286-class (fixed-point integer math, ~320x200)? Or modest modern (Raspberry Pi Zero, float OK, 640x480)? Or scalable? This decision affects the renderer's internals significantly.
- **Engine scope**: is this a pure "raycast + entities" engine (battleforge-style, game logic in the client), or does it include FPS primitives (health, weapons, enemy AI)? Lean toward pure engine, but unconfirmed.
- **Map authoring**: INI-based like barrier? Text grid? Image-based (pixels = cell types)? All viable.
- **Names**: for the engine lib and the client app. Project codename was not chosen.

## When resuming

1. Read this file and `README.md` alongside it for concept.
2. Read `libs/battleforge/battleforge.h` and `apps/barrier/main.c` to refresh the architectural pattern this should follow.
3. Start by writing the smallest possible `libs/raycast` that can render a textured wall: a map struct (cells in a grid), a DDA function returning a ray hit, a column-drawing function, and a `rc_render_chunk` entry point. Skip sprites and floors for the first milestone.
4. Get it running in a minimal SDL2 client before thinking about the engine layer. Prove the renderer works in isolation first.
5. Then build the engine lib on top, following battleforge's command/ECS shape.

## Related seeds

- `../raster-software/` — software triangle rasterizer (MDK-style). Completely different rendering technique; much bigger project.
- `../sphere-bricks/` and `../sphere-voxel/` — sphere-only worlds using `libs/raytrace`. Different aesthetic, different performance characteristics.
