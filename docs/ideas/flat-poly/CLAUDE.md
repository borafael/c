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

The conversation that seeded this idea asked: *"could a Dark Omen-like game (regimental tactics, hundreds of soldiers on a battlefield) work if the soldiers were actual polygon meshes instead of 2D billboard sprites?"* This section captures the math behind the answer so a future session doesn't have to rederive it.

The game-concept seed that came out of this analysis lives at **`../origami-armies/`** — see that folder for the game design, setting, unit types, and v1 scope. This section is just the engine-level feasibility analysis.

### Triangle budget per soldier

| Fidelity | Tris / soldier |
|---|---:|
| Stick figure / box body | 6-12 |
| Minimalist low-poly (box body + limb hints) | 12-30 |
| PS1-era low-poly (rough anatomy) | 50-150 |
| PS2-era low-poly | 300-600 |

### Software rasterizer capacity at realtime framerates

| Era / hardware | Triangles per frame (software) |
|---|---:|
| 486 (1993-ish) | ~200-500 |
| Pentium 75 (1995) | ~1,000 |
| Pentium 133 (1996) | ~2,000-3,000 |
| Pentium II 300 (1998) | ~5,000-10,000 |
| Modern Pi Zero 2 / cheap SBC (no GPU) | ~50,000-200,000 |

### Scenarios

**Scenario A — full Dark Omen scale**: 500 soldiers × 20 tris = 10,000 tris/frame
- 486 / Pentium 75: impossible (10-20× too slow)
- Pentium II 300: borderline, ~10-15 fps
- **Modern Pi Zero 2 class: comfortable 30-60 fps**

**Scenario B — smaller battles**: 200 soldiers × 30 tris = 6,000 tris/frame
- Pentium 75: marginal (~5-8 fps, not really playable)
- Pentium II: playable (20-30 fps)
- **Pi Zero 2 class: trivially comfortable**

**Scenario C — Shogun: Total War scale with LOD cascade**: 1,000+ soldiers, close units at 300 tris, mid at 50 tris, far as sprites. Total ~16,500 tris + sprite overhead.
- Historically shipped in 2000 on Pentium II + 3D acceleration (Shogun: Total War itself)
- Pure software on period hardware: sluggish
- **Modern Pi Zero 2 class: very comfortable**

### The honest answer

"Old hardware" has two meanings in this repo and they give different answers:

1. **Period-correct 1998 hardware, pure software**: **No**, not at Dark Omen scale with polygonal soldiers. Dark Omen used sprites for a mathematical reason — 500 × 20 triangles was ~10× what software rendering could do at the time. Shogun: Total War (2000) is the historical proof that it took two more years plus hardware acceleration to make polygonal regimental tactics work.

2. **Modern low-spec without a GPU** (Pi Zero 2, cheap ARM SBCs, old netbooks): **Yes, comfortably**. Modern CPUs without GPUs are 50-200× faster than a Pentium 75. A naive software rasterizer has the budget for 500-1000 polygonal soldiers at 20-50 triangles each, without needing any 1996-era tricks (no BSP, no PVS, no surface caching, no mandatory LOD cascade).

For this repo, "low spec" has consistently meant interpretation (2). Under that interpretation, polygonal regimental tactics are absolutely achievable, and `flat-poly` is the right renderer for them.

### Design choices that make it work at scale

- **Stylized low-poly soldiers, 20-30 triangles each.** Not realistic. "Tiny origami warriors" — art style embraces the polygon budget rather than fighting it. Distinctive, currently unoccupied visual niche.
- **Rigid-body animation, not skeletal skinning.** Each limb is a rigid mesh with a parent transform. Animation is just rotating child transforms. Per-soldier animation cost: a handful of matrix multiplies. 500 soldiers × ~5 mults each = ~2,500 mults per frame = trivial.
- **Unified flat-poly terrain.** Battlefield is a big flat-shaded mesh generated from a heightmap, rendered by the same flat-poly pipeline as the soldiers. Clean, single renderer, no compositing between different rendering techniques.
- **Regiments as engine-layer abstraction.** Individual soldiers are ECS entities; the player-facing logical unit is a regiment (position, facing, formation, morale, orders). The engine spawns N soldier entities per regiment and drives their positions from formation rules. Battleforge's ECS pattern already accommodates this with a small extension.
- **Aggressive frustum culling.** In a rotating isometric-ish camera, roughly half the battlefield is typically off-screen. Skipping transforms on off-screen soldiers halves the effective triangle cost.

### Historical reference

**Shogun: Total War** (Creative Assembly, 2000) is the closest existing game to this idea. Fully polygonal regimental tactics, regiments of ~60-100 individual polygonal soldiers, battles of 10,000+ total units with LOD cascading to sprites at distance. Shipped on Pentium II / III hardware with optional 3D acceleration; the pure-software mode was compromised even on period hardware. Pre-dates the "modern low-spec hardware" class entirely — making `origami-armies` on `flat-poly` is essentially "what Shogun would have looked like if you built it for a Raspberry Pi in 2026 with no GPU."
