# Heightfield Terrain System Design

## Overview

Add a heightfield terrain primitive to the raytracer and integrate it into the battleforge engine, replacing the flat ground plane with rolling, height-varied terrain inspired by Warhammer: Dark Omen. Entities snap to terrain height. The system is designed for a 10v10 unit RTS with raytraced rendering.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Grid resolution | 64x64 vertices (scalable) | Sufficient for rolling hills, fast ray intersection, easy to increase later. Yields 63x63 cells. |
| Terrain authoring | Hardcoded sine-wave test map | Get rendering working fast, PNG heightmap loading is a follow-up |
| Height range | 0-10 world units (dramatic) | Creates tactical elevation; camera-terrain collision deferred |
| Coloring | Height-based base color + Phong lighting | Rich visuals with minimal complexity |
| Ray intersection | DDA grid traversal inside raytracer | Clean layer separation — raytracer owns the optimization |
| Entity interaction | Ground snapping via bilinear height query | Entities stand correctly on slopes |

## Terminology

- **Vertex**: A grid point with a height value. A 64x64 grid has 64x64 = 4096 vertices.
- **Cell**: The quad between 4 adjacent vertices. A 64x64 vertex grid has 63x63 = 3969 cells. Each cell is split into 2 triangles for ray intersection.

## Architecture

The terrain system spans two layers while preserving their separation:

```
main.c                  → calls bf_map_generate_test_terrain() at startup
                        → updates bf_set_map() call (remove r/g/b, add grid dims)

battleforge.h/c         → owns bf_map terrain data (heights, colors, normals, grid dims)
                        → generates test terrain (allocates heights, colors, normals)
                        → provides bf_map_height_at() for entity snapping
                        → adds heightfield to raytracer scene (replaces flat plane)
                        → snaps entity Y position each frame (XZ-only movement)
                        → picks against heightfield via rt_intersect_heightfield()
                        → frees terrain data in bf_destroy()

raytrace.h/c            → new heightfield arrays in rt_scene (like spheres[], planes[])
                        → DDA grid traversal for fast ray-heightfield intersection
                        → per-cell triangle construction and normal interpolation
                        → height-based coloring with Phong shading
                        → exposes rt_intersect_heightfield() for picking use
```

**Key principle:** The raytracer knows nothing about the game. Battleforge hands it a heightfield the same way it hands it sprites or planes. The raytracer handles intersection internally using DDA, which is an implementation detail invisible to consumers.

## Component Design

### 1. Raytracer — Heightfield Primitive

**New data structure in `raytrace.h`:**

```c
typedef struct {
    float *heights;           // 2D array (rows * cols) of vertex heights
    uint8_t *colors;          // (rows-1) * (cols-1) * 3 bytes (RGB per cell)
    float *normals;           // rows * cols * 3 floats (precomputed vertex normals)
    int rows, cols;           // Vertex grid dimensions (e.g., 64x64)
    float world_width;        // Total extent on X axis
    float world_depth;        // Total extent on Z axis
    float origin_x, origin_z; // World position of grid corner (0,0)
    float max_height;         // Maximum height (for AABB early-out)
} rt_heightfield;
```

**Ownership:** The raytracer borrows (does not copy) all pointer data (`heights`, `colors`, `normals`). The caller (battleforge `bf_map`) owns the lifetime and must keep data alive for the duration of rendering. This is consistent with how `rt_sprite.frames` works.

**New API — follows existing pattern:**

The raytracer uses separate arrays for each primitive type (spheres[], planes[], etc.). The heightfield follows this same pattern:

```c
int rt_scene_add_heightfield(rt_scene *scene, const rt_heightfield *hf);
```

- Stores a copy of the `rt_heightfield` struct (not the pointed-to data) in the scene's `heightfields[]` array
- Returns the heightfield index, consistent with other `rt_scene_add_*` functions
- The scene gains `rt_heightfield *heightfields`, `int heightfield_count`, `int heightfield_cap` fields

Additionally, expose the intersection function for picking:

```c
int rt_intersect_heightfield(const rt_heightfield *hf, vector origin, vector dir,
                              float *out_t, vector *out_normal, int *out_cell_r, int *out_cell_c);
```

Returns 1 on hit, 0 on miss. Outputs the hit distance `t`, surface normal, and cell coordinates.

**Ray intersection algorithm (DDA):**

0. AABB early-out: test ray against the heightfield bounding box (origin_x..origin_x+world_width, 0..max_height, origin_z..origin_z+world_depth) using existing box intersection logic. Skip DDA if missed.
1. Project the ray onto the XZ plane
2. Determine which grid cell the ray enters first
3. Step through cells using DDA (like Bresenham but for ray marching through a grid)
4. In each visited cell, construct 2 triangles from the 4 corner heights
5. Test ray against both triangles; if hit, return the intersection
6. If the ray exits the grid bounds, return no hit

**Normals:** Precomputed per-vertex by `bf_map_generate_test_terrain()` and stored in the `normals` array. Each vertex normal is the average of its adjacent face normals. At intersection time, the normal is interpolated from the triangle's 3 vertex normals using barycentric coordinates — no per-pixel normal computation needed.

**Coloring:** The cell's RGB color from the `colors` array is used as the base, then Phong shading is applied using the existing directional light and ambient settings.

### 2. Battleforge — Terrain Data

**Extended `bf_map` in `battleforge.h`:**

```c
typedef struct {
    float width, depth;
    int grid_cols, grid_rows;   // Vertex dimensions (e.g., 64x64)
    float *heights;             // grid_rows * grid_cols floats
    uint8_t *colors;            // (grid_rows-1) * (grid_cols-1) * 3 (RGB per cell)
    float *normals;             // grid_rows * grid_cols * 3 (precomputed vertex normals)
    float max_height;           // 10.0 for dramatic terrain
    float ambient;
    vector light_dir;
    float light_intensity;
} bf_map;
```

**Breaking change:** The existing `uint8_t r, g, b` fields are removed since the per-cell color array replaces the single ground color.

**New functions in `battleforge.h`:**

```c
void  bf_map_generate_test_terrain(bf_map *map);
float bf_map_height_at(bf_map *map, float x, float z);
```

**Test terrain generation (`bf_map_generate_test_terrain`):**

- Allocates `heights`, `colors`, and `normals` arrays (caller sets `grid_rows`, `grid_cols`, `width`, `depth`, `max_height` before calling)
- Fills height array using layered sine waves for natural-looking rolling hills:
  `h = sin(x*0.3) * cos(z*0.2) * 5.0 + sin(x*0.7 + z*0.5) * 2.5`
- Normalizes to 0..max_height range
- Assigns per-cell colors based on height:
  - Low (0-30%): dark green `(40, 120, 40)` — valleys
  - Mid (30-70%): light green `(80, 160, 60)` — fields
  - High (70-100%): brown/grey `(140, 110, 70)` — hilltops
- Precomputes vertex normals by averaging adjacent face normals

**Height query (`bf_map_height_at`):**

- Converts world (x, z) to grid coordinates
- Bilinear interpolation between the 4 nearest vertex heights
- Clamps to grid bounds for out-of-range queries

**Memory management:** `bf_destroy()` is updated to free `map.heights`, `map.colors`, and `map.normals`.

### 3. Battleforge — Scene Building Change

In the per-frame scene building (currently adds a flat plane):

- Remove the `rt_scene_add_plane()` call for the ground
- Replace with `rt_scene_add_heightfield()` passing the map's terrain data

### 4. Entity Ground Snapping

In the entity update loop:

- Entity movement uses **XZ-only distance and direction**. The current code computes `to_target = target - position` as a 3D vector, which includes the Y component. This must be projected to XZ: set `to_target.y = 0` before computing magnitude and normalizing. This prevents terrain height from affecting movement speed or distance calculations.
- After updating entity XZ position (movement toward target): `entity.position.y = bf_map_height_at(map, entity.position.x, entity.position.z)`
- When a move command is issued, the target Y is also resolved via `bf_map_height_at()`

### 5. Picking Update

Ground picking currently manually intersects the Y=0 plane in `bf_pick()`. This is replaced by calling `rt_intersect_heightfield()` directly (the exposed raytracer function), passing the map's heightfield data and the pick ray. This gives correct world-space hit positions on the terrain surface.

## Files Changed

| File | Change |
|------|--------|
| `libs/raytrace/raytrace.h` | New `rt_heightfield` struct, `rt_scene_add_heightfield()` and `rt_intersect_heightfield()` declarations, heightfield fields in `rt_scene` |
| `libs/raytrace/raytrace.c` | DDA intersection, triangle construction, normal interpolation, heightfield coloring, AABB early-out (~150-200 new lines) |
| `libs/battleforge/battleforge.h` | Extended `bf_map` (remove r/g/b, add grid/heights/colors/normals), `bf_map_generate_test_terrain()` and `bf_map_height_at()` declarations |
| `libs/battleforge/battleforge.c` | Test terrain generation with normal precomputation, height query, entity Y-snapping with XZ-only movement, swap plane for heightfield, picking via `rt_intersect_heightfield()`, free terrain data in `bf_destroy()` |
| `apps/battleforge/main.c` | Update `bf_set_map()` call (remove r/g/b, set grid_cols/grid_rows/max_height), call `bf_map_generate_test_terrain()` at startup |

**Not changed:** `libs/slice/`, `libs/math/vector.h`, sprite system, command system, entity creation/animation.

## Known Follow-ups (Out of Scope)

- **Camera-terrain collision** — Prevent camera from going inside hills (needed because height range is dramatic at 0-10 units with camera at Y=15)
- **PNG heightmap loading** — Load grayscale PNG as height data for hand-authored maps
- **Walkability grid** — Per-cell flags for pathfinding (blocked, slow, etc.)
- **Pathfinding (A\*)** — Route entities around obstacles using walkability grid
- **Per-cell terrain types** — Affect unit movement speed, visual footstep effects
