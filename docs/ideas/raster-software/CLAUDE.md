# raster-software — resume context

**Status:** seeded, not started. No code exists yet.

**Brainstormed:** 2026-04-08, during a conversation about low-spec software 3D engines. The MDK reference came up partway through and clarified that a raycaster is the wrong tool for MDK-shaped gameplay; a software rasterizer is what's actually needed for that direction.

## What's been decided

- **MDK is not a raycaster game.** It's a software triangle rasterizer rendering arbitrary meshes. If an MDK-like game is the goal, a raycaster engine cannot substitute — the techniques are categorically different.
- **`libs/raytrace` also cannot substitute.** It handles triangles, but raytracing per-pixel is inherently slower than rasterizing per-triangle for the same scene. Using it for an MDK-style game means giving up the "low spec" goal entirely.
- **This must be a new lib**: `libs/raster`, sibling to `libs/raytrace`.
- **Engine layer on top**: mirrors the battleforge pattern, same as the other seeded ideas.
- **Scope is large**: explicitly acknowledged as the biggest of the four seeded projects.

## What's still open

- **Everything about the rasterizer internals.** Fixed-point vs float, affine vs perspective-correct texture mapping, tile-based vs scanline-based, near-plane clipping vs full frustum clipping, z-buffer precision format.
- **Mesh format**: custom binary, OBJ importer, or both?
- **Animation**: no animation initially? Skinned characters later? Vertex morphs?
- **Target hardware**: Pentium-class is the mental model (since that's what MDK ran on), but realistically this would ship to a Raspberry Pi / cheap ARM SBC with modern precision — the constraint is "no GPU," not "literal 1997 hardware."
- **Game concept**: what's actually being built on top? MDK-inspired is the aesthetic, but the gameplay pitch is blank.
- **Names**: for the engine lib and the client app.

## When resuming

**Strongly consider**: do NOT start this as the first software-3D project in this monorepo. Build `raycast-grid`, `sphere-bricks`, or `sphere-voxel` first to practice the engine-on-renderer pattern on an easier renderer. The engine-layer lessons transfer perfectly; the rasterizer itself is the hard part and benefits from being tackled last, when the architectural decisions around it are already shaken out.

**When you do resume:**

1. Re-read this file and `README.md`.
2. Re-read `libs/battleforge/battleforge.h` and `libs/raytrace/raytrace.h` to recall the engine-on-renderer pattern.
3. Start with the **minimum viable rasterizer**: solid-color flat-shaded triangles with a z-buffer, single triangle at a time, no texturing, no clipping beyond trivial. Get one rotating cube on screen. Nothing else.
4. Add near-plane clipping next (the second-simplest thing that actually has to work, because without it you get NaN corruption on triangles crossing the camera).
5. Add affine texture mapping (the PS1 look). Ship at this point if appropriate — it's a real aesthetic.
6. Add perspective-correct texture mapping later as a quality upgrade.
7. Only then start thinking about the engine layer on top.

**Reference reading if stuck**: Fabien Sanglard's "Game Engine Black Book: Doom" is excellent for software-3D techniques of the era, as is Michael Abrash's "Zen of Graphics Programming."

## Related seeds

- `../raycast-grid/` — Wolf3D-style raycaster. Completely different technique, much smaller project, also targets low-spec.
- `../sphere-bricks/` and `../sphere-voxel/` — sphere-only worlds using `libs/raytrace`. Render arbitrary 3D scenes (not grid-locked) without a rasterizer, but only with spheres as primitives.
