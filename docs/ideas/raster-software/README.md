# raster-software

An MDK-style **software triangle rasterizer** engine. The goal: render arbitrary 3D polygon meshes at playable frame rates on weak hardware with no GPU involvement — the same philosophy that let MDK (Shiny, 1997) ship a sniper-scope-wielding, free-look 3D action game on a Pentium 75.

## The core idea

A rasterizer is primitive-driven: for each triangle, figure out which screen pixels it covers and shade them. This is fundamentally different from (and much faster than) a software raytracer for the same scene, because the rasterizer only touches pixels that are actually covered, and it can interpolate values (depth, texture coordinates, color) along scanlines incrementally.

Software rasterizers were the standard for PC 3D games from roughly 1993 to 1998 — Doom was arguably the last hybrid, Quake was the first fully 3D one, and games like MDK and Unreal were the end of the road before 3D accelerators took over completely. The technique is well-understood but genuinely nontrivial to implement correctly.

## Target aesthetic and gameplay feel

- Arbitrary 3D polygon meshes — characters, vehicles, organic terrain
- Free 6-DOF camera; real looking up/down, not faked
- Large draw distances (exploit the "no overdraw" advantage of software rasterizing)
- Visual vibe: late-90s software-rendered 3D. Perspective distortions or clean texturing depending on implementation choices.
- Something bizarre and beautiful like MDK's surreal levels is the aspiration

## Planned structure

```
libs/raster         software triangle rasterizer (setup, edge fns, textures, z-buffer, clipping)
libs/<engine>       game engine layered on raster
apps/<client>       SDL2 client
```

`libs/raster` would be a sibling to `libs/raytrace`, exposing a chunkable render entry point that takes a list of meshes + camera and fills a pixel buffer.

## What the rasterizer lib needs (non-trivial list)

This is the honest scope — a competent software rasterizer is a substantial piece of code:

- **Triangle setup**: given three projected vertices, compute edge functions and setup data for the inner loop.
- **Inner loop**: fill scanlines, interpolate depth and UVs.
- **Z-buffer** (or 1/z buffer) for occlusion.
- **Clipping**: against the near plane at minimum, probably all frustum planes.
- **Texture mapping**: affine first (simpler, gives a PlayStation 1 look), perspective-correct later for the "PC 1997" look.
- **Backface culling** and **frustum culling**.
- **Mesh format**: vertices, indices, UVs. Loader from some format (custom binary, or OBJ).
- Later: skinning for animated characters, vertex morphs, etc.

Each of those bullet points is its own well of details. This is the largest of the four seeded ideas by a wide margin.

## What it would reuse from existing libs

- `math` — matrices and vectors
- `ds` — dynamic arrays for meshes and draw lists
- `ini` — scene/level config
- `thread` — parallelize across screen tiles (standard software rasterizer pattern)
- `slice` — possibly for 2D HUD elements, animated sprites
- **Not** `libs/raytrace` — different technique. `raytrace` would still render the same scenes (it handles triangles), but at a per-pixel cost that defeats the point of "low-spec 3D."

## Why this project exists separately from raycast-grid

A raycaster can only render grid worlds. A rasterizer can render anything. They're in different categories and solve different problems. raycast-grid maxes out the "trade dimensions for speed" axis; raster-software gives up that trade entirely in exchange for arbitrary geometry, and claws back speed through clever rasterization rather than constraint.

## Honest warning

This is a **much larger project** than the other three seeded ideas. A competent raycaster can be working in a weekend; a competent software rasterizer is weeks of work minimum, and the first several days will be fighting texture mapping artifacts, clipping bugs, and z-buffer precision issues. Do not start this before at least one of the smaller seeded projects is shipped — the engine-on-renderer pattern is best practiced on an easier renderer first.
