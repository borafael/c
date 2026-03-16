# Heightfield Terrain System Design

## Overview

Add a heightfield terrain primitive to the raytracer and integrate it into the battleforge engine, replacing the flat ground plane with rolling, height-varied terrain inspired by Warhammer: Dark Omen. Entities snap to terrain height. The system is designed for a 10v10 unit RTS with raytraced rendering.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Grid resolution | 64x64 (scalable) | Sufficient for rolling hills, fast ray intersection, easy to increase later |
| Terrain authoring | Hardcoded sine-wave test map | Get rendering working fast, PNG heightmap loading is a follow-up |
| Height range | 0-10 world units (dramatic) | Creates tactical elevation; camera-terrain collision deferred |
| Coloring | Height-based base color + Phong lighting | Rich visuals with minimal complexity |
| Ray intersection | DDA grid traversal inside raytracer | Clean layer separation — raytracer owns the optimization |
| Entity interaction | Ground snapping via bilinear height query | Entities stand correctly on slopes |

## Architecture

The terrain system spans two layers while preserving their separation:

```
main.c                  → calls bf_map_generate_test_terrain() at startup
                           (no other changes)

battleforge.h/c         → owns bf_map terrain data (heights, colors, grid dims)
                        → generates test terrain
                        → provides bf_map_height_at() for entity snapping
                        → adds heightfield to raytracer scene (replaces flat plane)
                        → snaps entity Y position each frame

raytrace.h/c            → new RT_HEIGHTFIELD primitive type
                        → DDA grid traversal for fast ray-heightfield intersection
                        → per-cell triangle construction and normal interpolation
                        → height-based coloring with Phong shading
```

**Key principle:** The raytracer knows nothing about the game. Battleforge hands it a heightfield the same way it hands it sprites or planes. The raytracer handles intersection internally using DDA, which is an implementation detail invisible to consumers.

## Component Design

### 1. Raytracer — Heightfield Primitive

**New data structure in `raytrace.h`:**

```c
typedef struct {
    float *heights;           // 2D array (rows * cols) of height values
    int rows, cols;           // Grid dimensions (e.g., 64x64)
    float world_width;        // Total extent on X axis
    float world_depth;        // Total extent on Z axis
    float origin_x, origin_z; // World position of grid corner (0,0)
} rt_heightfield;
```

**New API:**

```c
void rt_scene_add_heightfield(rt_scene *s, rt_heightfield *hf, uint8_t *colors);
```

- `colors` is a flat array of `rows * cols * 3` bytes (RGB per cell)
- Adds a single heightfield to the scene's primitive list as type `RT_HEIGHTFIELD`

**Ray intersection algorithm (DDA):**

1. Project the ray onto the XZ plane
2. Determine which grid cell the ray enters first
3. Step through cells using DDA (like Bresenham but for ray marching through a grid)
4. In each visited cell, construct 2 triangles from the 4 corner heights
5. Test ray against both triangles; if hit, return the intersection
6. If the ray exits the grid bounds, return no hit

**Normals:** Interpolated from vertex normals at the triangle hit point. Vertex normals are computed by averaging the face normals of adjacent cells, giving smooth shading across the terrain.

**Coloring:** The cell's RGB color from the `colors` array is used as the base, then Phong shading is applied using the existing directional light and ambient settings.

### 2. Battleforge — Terrain Data

**Extended `bf_map` in `battleforge.h`:**

```c
typedef struct {
    float width, depth;
    int grid_cols, grid_rows;
    float *heights;           // grid_rows * grid_cols floats
    uint8_t *colors;          // grid_rows * grid_cols * 3 (RGB per cell)
    float max_height;         // 10.0 for dramatic terrain
    float ambient;
    vector light_dir;
    float light_intensity;
} bf_map;
```

**New functions in `battleforge.h`:**

```c
void  bf_map_generate_test_terrain(bf_map *map);
float bf_map_height_at(bf_map *map, float x, float z);
```

**Test terrain generation (`bf_map_generate_test_terrain`):**

- Fills height array using layered sine waves for natural-looking rolling hills:
  `h = sin(x*0.3) * cos(z*0.2) * 5.0 + sin(x*0.7 + z*0.5) * 2.5`
- Normalizes to 0..max_height range
- Assigns per-cell colors based on height:
  - Low (0-30%): dark green `(40, 120, 40)` — valleys
  - Mid (30-70%): light green `(80, 160, 60)` — fields
  - High (70-100%): brown/grey `(140, 110, 70)` — hilltops

**Height query (`bf_map_height_at`):**

- Converts world (x, z) to grid coordinates
- Bilinear interpolation between the 4 nearest grid points
- Clamps to grid bounds for out-of-range queries

### 3. Battleforge — Scene Building Change

In the per-frame scene building (currently adds a flat plane):

- Remove the `rt_scene_add_plane()` call for the ground
- Replace with `rt_scene_add_heightfield()` passing the map's height and color data

### 4. Entity Ground Snapping

In the entity update loop:

- After updating entity XZ position (movement toward target): `entity.position.y = bf_map_height_at(map, entity.position.x, entity.position.z)`
- When a move command is issued, the target Y is also resolved via `bf_map_height_at()`

### 5. Picking Update

Ground picking currently intersects the Y=0 plane. Since the heightfield is now a raytracer primitive, ground picking gets terrain-aware intersection automatically — the raytracer's hit test returns the heightfield hit point with correct world coordinates.

## Files Changed

| File | Change |
|------|--------|
| `libs/raytrace/raytrace.h` | New `rt_heightfield` struct, `RT_HEIGHTFIELD` enum value, `rt_scene_add_heightfield()` declaration |
| `libs/raytrace/raytrace.c` | DDA intersection, triangle construction, normal interpolation, heightfield coloring (~150-200 new lines) |
| `libs/battleforge/battleforge.h` | Extended `bf_map`, `bf_map_generate_test_terrain()` and `bf_map_height_at()` declarations |
| `libs/battleforge/battleforge.c` | Test terrain generation, height query, entity Y-snapping, swap plane for heightfield, picking update |
| `apps/battleforge/main.c` | Call `bf_map_generate_test_terrain()` at startup |

**Not changed:** `libs/slice/`, `libs/math/vector.h`, sprite system, command system, entity creation/animation.

## Known Follow-ups (Out of Scope)

- **Camera-terrain collision** — Prevent camera from going inside hills (needed because height range is dramatic at 0-10 units with camera at Y=15)
- **PNG heightmap loading** — Load grayscale PNG as height data for hand-authored maps
- **Walkability grid** — Per-cell flags for pathfinding (blocked, slow, etc.)
- **Pathfinding (A\*)** — Route entities around obstacles using walkability grid
- **Per-cell terrain types** — Affect unit movement speed, visual footstep effects
