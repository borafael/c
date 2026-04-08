# Ideas

Seeded project concepts that haven't been started yet. Each subfolder holds:

- **`README.md`** — the concept, target aesthetic, key technical choices, and what makes it distinctive. For humans (and future Claude sessions) to get oriented quickly.
- **`CLAUDE.md`** — context for resuming work: what was decided during brainstorming, what's still open, and what the next concrete step would be.

Unlike `docs/plans/` (active work) or `docs/superpowers/specs/` (designs being implemented), ideas here are unstarted and may never be built. Seeding them ensures the thought work isn't lost.

## Current seeds

| Folder | Concept | Status |
|---|---|---|
| [`raycast-grid/`](raycast-grid/) | Wolfenstein 3D-style grid raycaster — maximally low-spec retro FPS feel | seeded |
| [`raster-software/`](raster-software/) | MDK-style software triangle rasterizer — Pentium-class low-spec 3D | seeded |
| [`sphere-bricks/`](sphere-bricks/) | Worlds built entirely from spheres, rendered with existing `libs/raytrace` unchanged | seeded |
| [`sphere-voxel/`](sphere-voxel/) | Sphere-voxel renderer — 3D uniform grid + DDA + sphere intersection | seeded |
| [`voxel-space/`](voxel-space/) | Comanche-style heightmap marcher — vast outdoor terrain on 386-class hardware | seeded |
| [`flat-poly/`](flat-poly/) | Elite/Stunts-style flat-shaded polygonal renderer — minimum-viable software 3D | seeded |

## The shared architectural pattern

All six ideas are expected to follow the same layered pattern already used in this repo by `libs/raytrace` + `libs/battleforge` + `apps/barrier`:

```
apps/<client>       SDL2 client (input, windowing, main loop)
    ↓
libs/<engine>       game engine (ECS, commands, picking, level loading)
    ↓
libs/<renderer>     renderer (pixel buffer in, pixel buffer out)
```

The engine layer is mostly reusable across renderers — what changes between ideas is the renderer underneath and the map format it consumes. This is the explicit design goal: keep engine APIs renderer-agnostic (world-space floats, opaque map handles) so the layer above survives a renderer swap.

Reusable libs available to all ideas: `math`, `slice`, `ini`, `ds`, `thread`, and (for some) `raytrace`.
