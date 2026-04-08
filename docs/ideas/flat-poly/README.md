# flat-poly

A minimum-viable **fully polygonal 3D engine** in the lineage of Elite (1984), Stunts (1990), Hard Drivin' (1989), X-Wing (1993), and Starglider (1986). Flat-shaded triangles, painter's-algorithm depth sorting, deliberately no textures and no z-buffer. The whole point is how *little* code it takes to render real 3D polygon meshes — the Elite tier of software rendering, not the Quake tier.

## The core idea

A software triangle rasterizer where every triangle is filled with **a single solid color**, triangles are sorted **back-to-front per frame** and drawn in that order, and the whole pipeline is ~hundreds of lines of code rather than thousands. You're trading the late-90s feature set (texture mapping, per-pixel lighting, z-buffer occlusion) for simplicity and hardware frugality.

The inner loop is genuinely tiny:

1. Transform mesh vertices to camera space, then to screen space (project + viewport)
2. Backface-cull triangles via their screen-space winding
3. Sort remaining triangles by depth (average Z of their vertices, or their centroid)
4. Rasterize each triangle as a solid-color scanline fill

No UV interpolation. No texture sampling. No z-buffer writes. No perspective divides in the inner loop (the only divides are at projection time). It is quite possibly the simplest fully-3D renderer that still looks like a game.

## Target aesthetic

The mid-80s to early-90s "vector graphics" look. Think:

- **Elite** (1984) — flat-shaded space ships, wireframe planets on a 2 MHz BBC Micro
- **Starglider** (1986) — flat-shaded abstract enemies
- **Stunts** (1990) — flat-shaded cars and tracks with tree sprites
- **Hard Drivin'** (1989, arcade) — flat-shaded open world
- **Virtua Racing** (1992, arcade) — flat-shaded high-performance racers
- **X-Wing / TIE Fighter** (1993-1994) — flat/gouraud space combat
- **Alone in the Dark** (1992) — polygonal characters on pre-rendered 2D backgrounds

This aesthetic has an almost-abstract quality. Solid colors read as surfaces; silhouettes do a lot of the visual work; the lack of texture detail gives everything a clean, graphic-design feel. It's distinctive, it's currently unoccupied in the game market (no one is shipping games that look like this on purpose in 2026), and it looks *nothing* like the other five seeded ideas.

## Why this is separate from `raster-software`

`raster-software` (the MDK seed) is the **late-90s** target: perspective-correct texture mapping, z-buffering, near/frustum clipping, mesh skinning, affine vs perspective tradeoffs, etc. It's the largest of all the seeded ideas by a wide margin — *weeks* of renderer work before you have a working base.

`flat-poly` is the **mid-80s to early-90s** target: flat colors, painter's algorithm, no textures. **Weekend-to-a-week of renderer work** for the minimum viable version. Different aesthetic, different scope, different project altogether. They're siblings, not competitors.

Think of it this way: every software rasterizer fits on a complexity ladder:

1. Wireframe (line drawing only)
2. **flat-shaded with painter's algorithm ← you are here**
3. Flat-shaded with z-buffer
4. Gouraud shading
5. Affine texture mapping
6. Perspective-correct texture mapping
7. BSP / portal / PVS / surface caching (Quake-era tricks)
8. **Full modern software renderer ← raster-software is here**

`flat-poly` sits at rung 2, `raster-software` at rung 8. Nothing stops `flat-poly` from later being extended toward rung 4 or 5 if it feels natural, but that's explicitly out of scope for the initial project.

## Target hardware

**Anything.** Elite shipped flat-shaded polygons on a 2 MHz 6502 with 32 KB of RAM in 1984. Every scaled-down modern target (Raspberry Pi Zero, old netbook, cheap SBC, a 486 emulator if you feel like it) is drastically overqualified. The real constraint on `flat-poly` is code simplicity and aesthetic, not hardware performance.

## Planned structure

```
libs/flatpoly       flat-shaded triangle rasterizer (new)
libs/<engine>       engine layered on flatpoly (ECS, commands, meshes)
apps/<client>       SDL2 client
```

Renderer API shape, following the repo convention of `rt_render_chunk(...)`:

```c
void fp_render_chunk(uint32_t *pixel_buf, const fp_viewport *viewport,
                     int y_start, int y_end,
                     const fp_camera *camera, const fp_scene *scene);
```

`fp_scene` is a list of mesh instances, each with a position + orientation + mesh id + optional per-triangle color overrides. Meshes are loaded once and instanced — the whole scene might be 10-20 unique meshes with hundreds of instances.

## Mesh format

Deliberately minimal:

```c
typedef struct {
    int vertex_count;
    float *vertices;        // xyz triples
    int triangle_count;
    int *triangles;         // vertex index triples
    uint32_t *colors;       // one ARGB color per triangle
} fp_mesh;
```

No UVs, no normals, no material system, no tangents, no bones, no weights. Flat color per triangle, that's it. A mesh can be loaded from a custom text format or generated procedurally. OBJ loader is a nice-to-have but unnecessary for a first version.

## What it would reuse

- `libs/math` — matrices (finally!), vectors, transformations. This is the first seed where matrix math pulls its weight.
- `libs/ds` — mesh lists, draw lists, triangle sort buffers
- `libs/ini` — level / scene config
- `libs/thread` — parallelize across horizontal strips of scanlines (standard software rasterizer pattern)
- `libs/slice` — possibly for 2D HUD elements layered on the 3D frame

Does **not** reuse `libs/raytrace` — different rendering technique, and specifically different from the raytrace path this repo has been building on.

## Potential games it would support well

The aesthetic and the performance profile fit a surprisingly wide range:

- **Space combat** — Elite / X-Wing / Wing Commander lineage. Few big-ish ships per scene, flat-shaded, intense dogfights.
- **Racing** — Stunts / Hard Drivin'. Flat-shaded car + flat-shaded track, maybe billboarded trees.
- **Mech combat** — MechWarrior 1 / Battletech feel. Big slow flat-shaded walkers on flat-shaded terrain.
- **Flight sim** — early Microsoft Flight Sim, F-19. Terrain + landmarks + other aircraft.
- **Vector-graphics exploration** — walking-sim aesthetic with abstract polygonal landscapes, minimal color palette.
- **Regimental tactical battles with low-poly soldiers** — Shogun: Total War-style gameplay with each soldier as a 10-30 triangle model instead of a sprite. The flat-poly renderer is ideal for this: low per-triangle cost, hundreds of instances, no texture authoring burden.

That last one is particularly interesting because it's a natural evolution of the Dark Omen / battleforge direction already being explored in this repo, but rendered with polygons instead of sprites. See `CLAUDE.md` for the context behind this.
