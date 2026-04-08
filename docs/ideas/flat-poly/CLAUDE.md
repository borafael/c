# flat-poly — resume context

**Status:** seeded, not started. No code exists yet.

**Brainstormed:** 2026-04-08. Emerged from a conversation about "fully polygonal rendering on hardware with no GPU," specifically as a simpler sibling to `raster-software`. The key realization: the `raster-software` seed had implicitly framed itself as the MDK-era target (late 90s: textures, perspective-correct, z-buffer, mesh skinning) — but there's a *much* simpler tier of polygonal rendering (Elite / Stunts / X-Wing era) that deserves its own project because it has a distinct aesthetic and drastically smaller scope.

## What's been decided

- **Technique**: flat-shaded triangle rasterization with painter's-algorithm depth sorting. No textures, no z-buffer, no BSP, no surface caching. Deliberately minimum viable.
- **Scope**: weekend-to-a-week for a rotating-cube milestone, maybe a month for something that feels like a game. An order of magnitude smaller than `raster-software`.
- **Aesthetic**: mid-80s to early-90s flat-shaded 3D. Elite, Stunts, X-Wing, Starglider, Virtua Racing. Distinctive, currently unoccupied in the game market.
- **New renderer lib**: `libs/flatpoly` (name tentative), sibling to `libs/raytrace`.
- **Engine layer**: mirrors the battleforge pattern (commands, ECS, picking, level loading).
- **Mesh format**: vertex list + triangle index list + one ARGB color per triangle. No UVs, no normals, no materials, no rigs. Custom text or binary format; OBJ loader is optional and out of scope for v1.
- **Renderer signature**: follows the repo convention of a chunkable `fp_render_chunk(pixel_buf, viewport, y_start, y_end, camera, scene)`.
- **Reuses**: `libs/math` (matrices finally earn their keep), `libs/ds`, `libs/ini`, `libs/thread`, `libs/slice` (for HUD). Does **not** reuse `libs/raytrace`.
- **Target hardware**: literally anything. Elite ran on a 2 MHz 6502 in 1984. Any plausible low-spec modern target is drastically overqualified.
- **Not in scope** (for v1, could grow into v2): textures, z-buffer, gouraud shading, perspective-correct anything, BSP. All of those are steps on the ladder toward `raster-software`, and `flat-poly` deliberately stops well below them.

## What's still open

- **Name**: `libs/flatpoly` is a working name. Alternatives: `libs/fpoly`, `libs/vec3d`, `libs/elitepoly`.
- **Painter's algorithm granularity**: sort per-triangle (correct for most cases, bad for interpenetrating meshes) or per-object (cheaper, requires non-interpenetrating scenes). Probably start per-triangle and accept the interpenetration limitation.
- **Backface culling**: front-face or back-face winding convention? Configurable or fixed? Conventions vary by era.
- **Gouraud shading**: optional upgrade path. If added, each vertex gets a color (from per-vertex lighting via vertex normals) and the triangle interpolates across its surface. Still no textures.
- **Animation approach**: rigid hierarchies (bones with rotation, no deformation) or vertex morphing (keyframe vertex positions)? Rigid hierarchies are simpler to start; morphing is what early games like X-Wing used.
- **Near-plane clipping**: mandatory. Triangle clipping against the near plane is one of the two things that will bite you first (the other is painter's algorithm correctness on interpenetrating meshes).
- **Palette**: full 32-bit ARGB from day one, or a fixed small palette (16/64/256 colors) to lean into the retro aesthetic more strongly? Fixed palette is evocative; full color is more flexible.
- **Game concept**: space combat? racing? mech? regimental tactics? Not chosen.

## When resuming

1. Re-read this file and `README.md`.
2. Re-read `libs/raytrace/raytrace.h` to see the repo convention for a chunkable renderer signature.
3. **First milestone — single rotating cube.** A standalone SDL2 program with no engine layer that: defines a cube mesh (8 vertices, 12 triangles, 6 colors), transforms it over time, rasterizes it with backface culling and painter's algorithm, and shows it on screen. This alone proves the rasterizer core works and is the foundation of everything else.
4. **Second milestone — multi-mesh scene with interpenetration awareness.** Add a second mesh, draw both, verify painter's algorithm handles the common cases (unless they interpenetrate, which is a known limitation of the technique).
5. **Third milestone — near-plane clipping.** Without this, triangles crossing the camera produce garbage. One of the standard first-week bugs.
6. **Only then** — start thinking about the engine layer. Follow `libs/battleforge` as the pattern.

## Related seeds

- `../raster-software/` — Sibling at the complexity ladder's top rung. Same technique category (software polygon rendering) but vastly larger scope and late-90s aesthetic. If you ever find `flat-poly` growing features, recognize the drift toward `raster-software` territory and decide consciously: keep it simple, or commit to the bigger project.
- `../voxel-space/` — Completely different technique. Heightmap marching, not polygons. But comparable in "how simple the renderer is."
- `../raycast-grid/`, `../sphere-bricks/`, `../sphere-voxel/` — All non-polygonal approaches. `flat-poly` is the first and only polygonal renderer in the seeded set (below the full `raster-software` tier).

## Dark Omen crossover — the motivating use case

One of the specific conversations that motivated this seed was: "could a Dark Omen-like game (regimental tactics, hundreds of soldiers on a battlefield) work if the soldiers were actual polygon meshes instead of 2D billboard sprites?" That question has its own engineering trade-offs (triangle budgets, LOD cascades, animation costs), but the relevant point for *this* seed is:

**Flat-poly is the right renderer for that game** if you want it to work on low-spec. Low-poly soldiers (10-30 triangles each) + flat shading + painter's algorithm naturally produces a high-instance-count regimental battle scene within the hardware budget of a modern weak CPU. The exact feasibility on period hardware (Pentium-class) is borderline and discussed elsewhere; on a Raspberry Pi Zero 2 or similar it's comfortable.

The historical reference point is **Shogun: Total War** (Creative Assembly, 2000) — fully polygonal regimental tactics, shipped on Pentium II/III hardware with 3D acceleration. A pure-software `flat-poly` version on modern low-spec hardware is achievable.

If that game direction is pursued, this seed is where it should live. Consider also seeding the game concept separately if it becomes a committed project.
