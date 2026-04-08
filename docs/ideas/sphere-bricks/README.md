# sphere-bricks

Build game worlds using **only spheres** as the primitive, rendered with the existing `libs/raytrace` unchanged. No new renderer, no new acceleration structure — just a discipline of "every object in the scene is a sphere." Think Lego bricks, marbles, soap bubbles.

## The core idea

`libs/raytrace` already handles spheres — it's the simplest and cheapest primitive to intersect (about 10 FLOPs + a sqrt, cheaper than ray-triangle). What if the entire game world — walls, enemies, items, terrain — is constructed from spheres? No triangles. No boxes. Just spheres, placed freely in 3D space.

This is not a grid-based approach (see the sibling `sphere-voxel` seed for the grid variant). Spheres can be placed anywhere, at any size, at any color. The feeling is closer to assembling a model out of marbles than to placing voxels on a lattice.

## Target aesthetic and gameplay feel

- Distinctive visual identity: sphere-only worlds are **uncommon in games**. You'd be in unclaimed territory immediately.
- Possible vibes:
  - "Marble Madness in true 3D"
  - "Katamari if everything were glowing orbs"
  - "An alien city built from soap bubbles"
  - "A pointillist 3D painting you can walk through"
- Free camera, real 3D lighting (sphere normals are trivial: `(hit - center) / radius`)
- Animation is free — moving a sphere is just moving its center. No rigging, no skinning, no vertex transforms.
- Collision is trivial — sphere-sphere distance test. No GJK, no SAT.

## Why this project exists separately from sphere-voxel

`sphere-bricks` is **freeform placement**: the scene is a flat list of spheres, each with an arbitrary position. It's artistic — you arrange spheres however you want, at whatever sizes. No grid, no quantization. Performance comes from keeping scene counts modest (hundreds of spheres, not millions) and optionally adding a BVH later if needed.

`sphere-voxel` is the **grid-accelerated** variant: a 3D uniform grid where each cell optionally contains a sphere. This trades placement freedom for a massively accelerated renderer (3D DDA traversal) and scales to much larger scenes. It's voxel art, not Lego art.

Both seeded as separate projects because they have different aesthetics, different authoring workflows, and different performance ceilings.

## Planned structure

```
libs/raytrace       existing, used unchanged
libs/<engine>       game engine layered on raytrace, but scene building restricted to spheres
apps/<client>       SDL2 client
```

**No new renderer lib.** This is the key property: `sphere-bricks` is a self-imposed discipline on top of existing code, not a new rendering system. The whole point is that the renderer already exists.

The engine layer would likely be thinner than battleforge's, because there's no heightmap terrain to handle — the "map" is just a list of sphere objects. ECS components for entities might look like:

- `position` (vec3)
- `sphere_visual` (radius, color)
- `locomotion` (same as battleforge)
- `collision_radius` (trivially derived from visual)

## What it would reuse

- `libs/raytrace` — used directly, unchanged. Only the `rt_sphere` primitive is touched.
- `libs/math`, `libs/ini`, `libs/ds`, `libs/thread` — all relevant.
- `libs/slice` — possibly, for 2D HUD / UI elements rendered on top of the ray-traced frame.

## Performance expectations

`libs/raytrace` currently has no acceleration structure — it loops over all primitives per ray. That's fine for scenes with a few hundred spheres at modest resolutions (320x240 multithreaded, or 640x480 with aggressive tuning). Beyond ~1000 spheres the per-pixel cost becomes prohibitive without a BVH or grid. If scenes grow beyond that, the project naturally converges on `sphere-voxel`.

## Why this is interesting despite being "simple"

- **Zero new rendering code.** The entire project is engine + client + scene design. All the hard graphics work is already done.
- **Proves the raytrace abstractions are good** by exercising them in a new context.
- **Forces creative constraints** — the "only spheres" rule is an artistic limitation that tends to produce more interesting work than having full freedom.
- **Shortest path to a playable thing** of any of the four seeded ideas.
