# Ideas

Seeded project concepts that haven't been started yet. Each subfolder holds:

- **`README.md`** — the concept, target aesthetic, key technical choices, and what makes it distinctive. For humans (and future Claude sessions) to get oriented quickly.
- **`CLAUDE.md`** — context for resuming work: what was decided during brainstorming, what's still open, and what the next concrete step would be.

Unlike `docs/plans/` (active work) or `docs/superpowers/specs/` (designs being implemented), ideas here are unstarted and may never be built. Seeding them ensures the thought work isn't lost.

## Two kinds of seeds

Seeds in this folder fall into two categories:

- **Engine concepts** describe *how to render* — a new rendering technique, data format, or low-level engine approach. They're technical and architectural. Example: `raycast-grid/` (how do you render a Wolfenstein 3D-style grid world?).
- **Game concepts** describe *what game to build* — a genre, mechanic, setting, or gameplay pitch — on top of one (or more) existing engine concepts. They depend on engines and have different considerations (fiction, unit types, campaign structure, mechanics). Example: `origami-armies/` (what game do you build on top of `flat-poly/`?).

The distinction matters because mixing rendering-technique questions with game-design questions in a single seed makes both harder to reason about. A game concept can change its backing engine; an engine concept can host many different games.

## Engine concepts

Most target the "low-spec" theme (ranging from 286-class to modern Raspberry Pi class). One exception, noted below, deliberately breaks that theme to explore the opposite direction.

| Folder | Concept | Hardware target | Status |
|---|---|---|---|
| [`raycast-grid/`](raycast-grid/) | Wolfenstein 3D-style grid raycaster — maximally low-spec retro FPS feel | low-spec | seeded |
| [`raster-software/`](raster-software/) | MDK-style software triangle rasterizer — Pentium-class low-spec 3D | low-spec | seeded |
| [`sphere-bricks/`](sphere-bricks/) | Worlds built entirely from spheres, rendered with existing `libs/raytrace` unchanged | low-spec | seeded |
| [`sphere-voxel/`](sphere-voxel/) | Sphere-voxel renderer — 3D uniform grid + DDA + sphere intersection | low-spec | seeded |
| [`voxel-space/`](voxel-space/) | Comanche-style heightmap marcher — vast outdoor terrain on 386-class hardware | low-spec | seeded |
| [`flat-poly/`](flat-poly/) | Elite/Stunts-style flat-shaded polygonal renderer — minimum-viable software 3D | low-spec | seeded |
| [`gpu-raytrace/`](gpu-raytrace/) | Real-time GPU-accelerated raytracer built on `libs/raytrace` — targets RTX-class hardware | **high-spec** (breaks theme) | seeded |

## Game concepts

| Folder | Concept | Depends on | Status |
|---|---|---|---|
| [`origami-armies/`](origami-armies/) | Dark Omen / Shogun-style regimental tactics with polygonal low-poly soldiers — tiny origami armies | `flat-poly/` | seeded |
| [`barnstorm/`](barnstorm/) | Corncob 3D / F-19-style low-altitude flight combat, flat-shaded polygonal | `flat-poly/` | seeded |

## The shared architectural pattern

All engine concepts are expected to follow the same layered pattern already used in this repo by `libs/raytrace` + `libs/battleforge` + `apps/barrier`:

```
apps/<client>       SDL2 client (input, windowing, main loop)
    ↓
libs/<engine>       game engine (ECS, commands, picking, level loading)
    ↓
libs/<renderer>     renderer (pixel buffer in, pixel buffer out)
```

The engine layer is mostly reusable across renderers — what changes between ideas is the renderer underneath and the map format it consumes. This is the explicit design goal: keep engine APIs renderer-agnostic (world-space floats, opaque map handles) so the layer above survives a renderer swap.

Reusable libs available to all ideas: `math`, `slice`, `ini`, `ds`, `thread`, and (for some) `raytrace`.
