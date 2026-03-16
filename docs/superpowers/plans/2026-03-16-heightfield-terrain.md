# Heightfield Terrain Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the flat ground plane with a heightfield terrain that renders rolling hills via DDA-accelerated ray intersection, with height-based coloring, Phong shading, and entity ground snapping.

**Architecture:** New `rt_heightfield` primitive in the raytracer with internal DDA grid traversal. Battleforge owns terrain data (heights, colors, normals), generates a test map from sine waves, and passes the heightfield to the raytracer each frame. Entities snap to terrain height using bilinear interpolation.

**Tech Stack:** C, existing raytracer (`raytrace.h/c`), existing engine (`battleforge.h/c`), SDL2 for display.

**Spec:** `docs/superpowers/specs/2026-03-16-heightfield-terrain-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `libs/raytrace/raytrace.h` | Modify | Add `rt_heightfield` struct, `rt_scene_add_heightfield()`, `rt_intersect_heightfield()` declarations |
| `libs/raytrace/raytrace.c` | Modify | Add heightfield to `rt_scene`, DDA intersection, AABB early-out, triangle intersection with interpolated normals, heightfield rendering in `rt_render_chunk` |
| `libs/battleforge/battleforge.h` | Modify | Extend `bf_map` (remove r/g/b, add grid fields), add `bf_map_generate_test_terrain()` and `bf_map_height_at()` |
| `libs/battleforge/battleforge.c` | Modify | Terrain generation, height query, entity Y-snapping with XZ-only movement, swap plane for heightfield in render, picking via `rt_intersect_heightfield()`, free terrain data in `bf_destroy()` |
| `apps/battleforge/main.c` | Modify | Update `bf_set_map()` call, call `bf_map_generate_test_terrain()`, remove hardcoded entity Y=1.0 |

---

## Chunk 1: Raytracer Heightfield Primitive

### Task 1: Add rt_heightfield struct and scene storage

**Files:**
- Modify: `libs/raytrace/raytrace.h:56-67` (after `rt_box`, before `rt_light`)
- Modify: `libs/raytrace/raytrace.c:17-43` (add fields to `struct rt_scene`)
- Modify: `libs/raytrace/raytrace.c:45-75` (`rt_scene_create` — allocate heightfield array)
- Modify: `libs/raytrace/raytrace.c:77-86` (`rt_scene_clear` — reset heightfield count)
- Modify: `libs/raytrace/raytrace.c:141-152` (`rt_scene_destroy` — free heightfield array)

- [ ] **Step 1: Add `rt_heightfield` struct to `raytrace.h`**

Insert after the `rt_box` typedef (after line 62), before `rt_light`:

```c
typedef struct {
    float *heights;           /* rows * cols vertex heights (borrowed) */
    uint8_t *colors;          /* (rows-1)*(cols-1)*3 RGB per cell (borrowed) */
    float *normals;           /* rows * cols * 3 vertex normals (borrowed) */
    int rows, cols;           /* vertex grid dimensions */
    float world_width;        /* X extent in world units */
    float world_depth;        /* Z extent in world units */
    float origin_x, origin_z; /* world position of grid corner (0,0) */
    float max_height;         /* for AABB early-out */
} rt_heightfield;
```

- [ ] **Step 2: Add `rt_scene_add_heightfield()` and `rt_intersect_heightfield()` declarations to `raytrace.h`**

Insert after `rt_scene_add_sprite` (line 123):

```c
int rt_scene_add_heightfield(rt_scene *scene, const rt_heightfield *hf);

int rt_intersect_heightfield(const rt_heightfield *hf, vector origin, vector dir,
                              float *out_t, vector *out_normal,
                              int *out_cell_r, int *out_cell_c);
```

- [ ] **Step 3: Add heightfield fields to `struct rt_scene` in `raytrace.c`**

Add after the sprite fields (after line 42), before the closing `};`:

```c
    rt_heightfield *heightfields;
    int heightfield_count;
    int heightfield_capacity;
```

- [ ] **Step 4: Allocate heightfield array in `rt_scene_create`**

Add `s->heightfield_capacity = DEFAULT_CAPACITY;` alongside the other capacity initializations (around line 56).

Add `s->heightfields = malloc(sizeof(rt_heightfield) * s->heightfield_capacity);` alongside the other mallocs (around line 66).

Add `!s->heightfields` to the null check on line 68-70.

- [ ] **Step 5: Reset heightfield count in `rt_scene_clear`**

Add `scene->heightfield_count = 0;` alongside the other count resets (around line 84).

- [ ] **Step 6: Free heightfield array in `rt_scene_destroy`**

Add `free(scene->heightfields);` alongside the other frees (around line 149).

- [ ] **Step 7: Implement `rt_scene_add_heightfield`**

Add after `rt_scene_add_light` (after line 139):

```c
int rt_scene_add_heightfield(rt_scene *scene, const rt_heightfield *hf) {
    if (scene->heightfield_count >= scene->heightfield_capacity) return -1;
    scene->heightfields[scene->heightfield_count++] = *hf;
    return 0;
}
```

- [ ] **Step 8: Build and verify compilation**

Run: `cd /home/rafa/repos/c && make`

Expected: Compiles with no errors (the intersection function is declared but not yet defined — that's next task).

- [ ] **Step 9: Commit**

```bash
git add libs/raytrace/raytrace.h libs/raytrace/raytrace.c
git commit -m "feat(raytrace): add rt_heightfield struct and scene storage"
```

---

### Task 2: Implement DDA heightfield intersection

**Files:**
- Modify: `libs/raytrace/raytrace.c` (add static helpers and `rt_intersect_heightfield` implementation)

This is the core algorithm. It implements:
1. AABB early-out using the slab method
2. DDA grid traversal stepping through cells along the ray's XZ projection
3. Per-cell triangle intersection using the existing Möller-Trumbore algorithm
4. Interpolated normal from precomputed vertex normals via barycentric coordinates

- [ ] **Step 1: Add AABB early-out helper**

Add before the `rt_intersect_heightfield` implementation (after `rt_scene_add_heightfield`):

```c
/* AABB test for heightfield bounding box. Returns 1 if ray hits, sets t_enter/t_exit. */
static int hf_aabb_test(const rt_heightfield *hf, vector ro, vector rd,
                         float *t_enter, float *t_exit) {
    float xmin = hf->origin_x;
    float xmax = hf->origin_x + hf->world_width;
    float ymin = 0.0f;
    float ymax = hf->max_height;
    float zmin = hf->origin_z;
    float zmax = hf->origin_z + hf->world_depth;

    float tmin = -FLT_MAX, tmax = FLT_MAX;

    if (fabsf(rd.x) > 1e-6f) {
        float t0 = (xmin - ro.x) / rd.x;
        float t1 = (xmax - ro.x) / rd.x;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.x < xmin || ro.x > xmax) return 0;

    if (fabsf(rd.y) > 1e-6f) {
        float t0 = (ymin - ro.y) / rd.y;
        float t1 = (ymax - ro.y) / rd.y;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.y < ymin || ro.y > ymax) return 0;

    if (fabsf(rd.z) > 1e-6f) {
        float t0 = (zmin - ro.z) / rd.z;
        float t1 = (zmax - ro.z) / rd.z;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
    } else if (ro.z < zmin || ro.z > zmax) return 0;

    if (tmin > tmax) return 0;
    *t_enter = tmin > 0.0f ? tmin : 0.0f;
    *t_exit = tmax;
    if (tmax < 0.0f) return 0;
    return 1;
}
```

- [ ] **Step 2: Add triangle intersection helper with barycentric coordinates**

```c
/* Intersect ray with triangle, return t and barycentric coords (u, v). w = 1 - u - v. */
static float hf_intersect_tri(vector ro, vector rd,
                                vector v0, vector v1, vector v2,
                                float *out_u, float *out_v) {
    vector e1 = vector_sub(v1, v0);
    vector e2 = vector_sub(v2, v0);
    vector pvec = vector_cross(rd, e2);
    float det = vector_dot(e1, pvec);
    if (fabsf(det) < 1e-6f) return -1.0f;

    float inv_det = 1.0f / det;
    vector tvec = vector_sub(ro, v0);
    float u = vector_dot(tvec, pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) return -1.0f;

    vector qvec = vector_cross(tvec, e1);
    float v = vector_dot(rd, qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;

    float t = vector_dot(e2, qvec) * inv_det;
    if (t < 0.0f) return -1.0f;

    *out_u = u;
    *out_v = v;
    return t;
}
```

- [ ] **Step 3: Add vertex position and normal lookup helpers**

```c
/* Get world-space position of vertex (r, c) in the heightfield */
static vector hf_vertex_pos(const rt_heightfield *hf, int r, int c) {
    float cell_w = hf->world_width / (float)(hf->cols - 1);
    float cell_d = hf->world_depth / (float)(hf->rows - 1);
    return (vector){
        hf->origin_x + c * cell_w,
        hf->heights[r * hf->cols + c],
        hf->origin_z + r * cell_d
    };
}

/* Get precomputed vertex normal */
static vector hf_vertex_normal(const rt_heightfield *hf, int r, int c) {
    int idx = (r * hf->cols + c) * 3;
    return (vector){ hf->normals[idx], hf->normals[idx + 1], hf->normals[idx + 2] };
}
```

- [ ] **Step 4: Implement `rt_intersect_heightfield` with DDA**

```c
int rt_intersect_heightfield(const rt_heightfield *hf, vector origin, vector dir,
                              float *out_t, vector *out_normal,
                              int *out_cell_r, int *out_cell_c) {
    /* AABB early-out */
    float t_enter, t_exit;
    if (!hf_aabb_test(hf, origin, dir, &t_enter, &t_exit)) return 0;

    /* Grid cell dimensions */
    int cells_x = hf->cols - 1;  /* number of cells along X */
    int cells_z = hf->rows - 1;  /* number of cells along Z */
    float cell_w = hf->world_width / (float)cells_x;
    float cell_d = hf->world_depth / (float)cells_z;

    /* Entry point into the grid */
    float eps = 1e-4f;
    vector entry = vector_add(origin, vector_scale(dir, t_enter + eps));

    /* Convert to grid coordinates */
    float gx = (entry.x - hf->origin_x) / cell_w;
    float gz = (entry.z - hf->origin_z) / cell_d;

    int cx = (int)floorf(gx);
    int cz = (int)floorf(gz);

    /* Clamp to grid bounds */
    if (cx < 0) cx = 0; if (cx >= cells_x) cx = cells_x - 1;
    if (cz < 0) cz = 0; if (cz >= cells_z) cz = cells_z - 1;

    /* DDA step direction and t-deltas */
    int step_x = (dir.x >= 0.0f) ? 1 : -1;
    int step_z = (dir.z >= 0.0f) ? 1 : -1;

    /* Distance along ray to cross one cell in X and Z */
    float t_delta_x = (fabsf(dir.x) > 1e-6f) ? fabsf(cell_w / dir.x) : FLT_MAX;
    float t_delta_z = (fabsf(dir.z) > 1e-6f) ? fabsf(cell_d / dir.z) : FLT_MAX;

    /* Distance to the next cell boundary from entry point */
    float next_x_boundary = hf->origin_x + ((dir.x >= 0.0f) ? (cx + 1) : cx) * cell_w;
    float next_z_boundary = hf->origin_z + ((dir.z >= 0.0f) ? (cz + 1) : cz) * cell_d;

    float t_max_x = (fabsf(dir.x) > 1e-6f)
        ? (next_x_boundary - origin.x) / dir.x : FLT_MAX;
    float t_max_z = (fabsf(dir.z) > 1e-6f)
        ? (next_z_boundary - origin.z) / dir.z : FLT_MAX;

    /* Walk the grid */
    float best_t = FLT_MAX;
    vector best_normal = {0, 1, 0};
    int best_cr = -1, best_cc = -1;

    int max_steps = cells_x + cells_z + 2;
    for (int step = 0; step < max_steps; step++) {
        if (cx < 0 || cx >= cells_x || cz < 0 || cz >= cells_z) break;

        /* Cell (cz, cx) — row=cz, col=cx. Build 2 triangles from 4 corners. */
        vector v00 = hf_vertex_pos(hf, cz,     cx);
        vector v10 = hf_vertex_pos(hf, cz + 1, cx);
        vector v01 = hf_vertex_pos(hf, cz,     cx + 1);
        vector v11 = hf_vertex_pos(hf, cz + 1, cx + 1);

        vector n00 = hf_vertex_normal(hf, cz,     cx);
        vector n10 = hf_vertex_normal(hf, cz + 1, cx);
        vector n01 = hf_vertex_normal(hf, cz,     cx + 1);
        vector n11 = hf_vertex_normal(hf, cz + 1, cx + 1);

        /* Triangle A: v00, v10, v01 */
        float u, v;
        float t = hf_intersect_tri(origin, dir, v00, v10, v01, &u, &v);
        if (t > 0.0f && t < best_t) {
            best_t = t;
            float w = 1.0f - u - v;
            best_normal = vector_normalize((vector){
                w * n00.x + u * n10.x + v * n01.x,
                w * n00.y + u * n10.y + v * n01.y,
                w * n00.z + u * n10.z + v * n01.z
            });
            best_cr = cz;
            best_cc = cx;
        }

        /* Triangle B: v10, v11, v01 */
        t = hf_intersect_tri(origin, dir, v10, v11, v01, &u, &v);
        if (t > 0.0f && t < best_t) {
            best_t = t;
            float w = 1.0f - u - v;
            best_normal = vector_normalize((vector){
                w * n10.x + u * n11.x + v * n01.x,
                w * n10.y + u * n11.y + v * n01.y,
                w * n10.z + u * n11.z + v * n01.z
            });
            best_cr = cz;
            best_cc = cx;
        }

        /* If we found a hit in this cell, no need to continue — DDA visits cells
           in roughly front-to-back order, but a hit in the current cell may still
           be behind a hit in a later cell if the ray is very oblique. However for
           terrain this is rare. To be safe, break only if best_t < t_max_x and
           best_t < t_max_z (the hit is before we enter the next cell). */
        if (best_t < FLT_MAX && best_t <= fminf(t_max_x, t_max_z) + eps) break;

        /* Step to next cell */
        if (t_max_x < t_max_z) {
            cx += step_x;
            t_max_x += t_delta_x;
        } else {
            cz += step_z;
            t_max_z += t_delta_z;
        }
    }

    if (best_t < FLT_MAX) {
        *out_t = best_t;
        *out_normal = best_normal;
        if (out_cell_r) *out_cell_r = best_cr;
        if (out_cell_c) *out_cell_c = best_cc;
        return 1;
    }
    return 0;
}
```

- [ ] **Step 5: Build and verify compilation**

Run: `cd /home/rafa/repos/c && make`

Expected: Compiles with no errors.

- [ ] **Step 6: Commit**

```bash
git add libs/raytrace/raytrace.c
git commit -m "feat(raytrace): implement DDA heightfield ray intersection"
```

---

### Task 3: Integrate heightfield into rendering loop

**Files:**
- Modify: `libs/raytrace/raytrace.c:559-582` (in `rt_render_chunk`, after sprite loop)

- [ ] **Step 1: Add heightfield intersection loop in `rt_render_chunk`**

Insert after the Sprites loop (after line 582), before the `if (hit)` block (line 584):

```c
            /* Heightfields */
            for (int i = 0; i < scene->heightfield_count; i++) {
                const rt_heightfield *hf = &scene->heightfields[i];
                float t;
                vector hn;
                int cell_r, cell_c;
                if (rt_intersect_heightfield(hf, camera->origin, dir,
                                              &t, &hn, &cell_r, &cell_c)) {
                    if (t > 0.0f && t < closest_t) {
                        closest_t = t;
                        normal = hn;
                        /* Look up cell color */
                        int cells_per_row = hf->cols - 1;
                        int ci = (cell_r * cells_per_row + cell_c) * 3;
                        color.r = hf->colors[ci];
                        color.g = hf->colors[ci + 1];
                        color.b = hf->colors[ci + 2];
                        hit = 1;
                    }
                }
            }
```

- [ ] **Step 2: Build and verify compilation**

Run: `cd /home/rafa/repos/c && make`

Expected: Compiles with no errors.

- [ ] **Step 3: Commit**

```bash
git add libs/raytrace/raytrace.c
git commit -m "feat(raytrace): render heightfield terrain in ray loop"
```

---

## Chunk 2: Battleforge Terrain Data and Generation

### Task 4: Extend bf_map and add terrain functions

**Files:**
- Modify: `libs/battleforge/battleforge.h:22-29` (replace `bf_map` struct)
- Modify: `libs/battleforge/battleforge.h:84` (after `bf_set_map`, add new declarations)

- [ ] **Step 1: Replace `bf_map` struct in `battleforge.h`**

Replace lines 22-29:

```c
typedef struct {
    float width;
    float depth;
    int grid_cols, grid_rows;   /* vertex dimensions (e.g., 64x64) */
    float *heights;             /* grid_rows * grid_cols floats */
    uint8_t *colors;            /* (grid_rows-1) * (grid_cols-1) * 3 (RGB per cell) */
    float *normals;             /* grid_rows * grid_cols * 3 (vertex normals) */
    float max_height;           /* maximum terrain height */
    float ambient;
    vector light_dir;
    float light_intensity;
} bf_map;
```

- [ ] **Step 2: Add terrain function declarations to `battleforge.h`**

Insert after `bf_set_map` (after line 84):

```c
void  bf_map_generate_test_terrain(bf_map *map);
float bf_map_height_at(const bf_map *map, float x, float z);
```

- [ ] **Step 3: Do NOT build or commit yet**

The header change removes `r, g, b` fields which will break `battleforge.c` and `main.c`. Continue to Task 5 and Task 6 before building. All battleforge changes will be committed together in Task 6 to maintain a compilable state at every commit.

---

### Task 5: Implement terrain generation and height query

**Files:**
- Modify: `libs/battleforge/battleforge.c` (add implementations before `bf_create`)

- [ ] **Step 1: Implement `bf_map_generate_test_terrain`**

Add before `bf_create` (before line 83):

```c
void bf_map_generate_test_terrain(bf_map *map) {
    int rows = map->grid_rows;
    int cols = map->grid_cols;

    /* Allocate arrays */
    map->heights = calloc(rows * cols, sizeof(float));
    map->colors  = calloc((rows - 1) * (cols - 1) * 3, sizeof(uint8_t));
    map->normals = calloc(rows * cols * 3, sizeof(float));

    /* Generate heights from layered sine waves */
    float cell_w = map->width / (float)(cols - 1);
    float cell_d = map->depth / (float)(rows - 1);

    float h_min = FLT_MAX, h_max = -FLT_MAX;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            float wx = c * cell_w;
            float wz = r * cell_d;
            float h = sinf(wx * 0.3f) * cosf(wz * 0.2f) * 5.0f
                    + sinf(wx * 0.7f + wz * 0.5f) * 2.5f;
            map->heights[r * cols + c] = h;
            if (h < h_min) h_min = h;
            if (h > h_max) h_max = h;
        }
    }

    /* Normalize to 0..max_height */
    float range = h_max - h_min;
    if (range < 1e-6f) range = 1.0f;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            map->heights[r * cols + c] =
                ((map->heights[r * cols + c] - h_min) / range) * map->max_height;
        }
    }

    /* Assign per-cell colors based on average height of 4 corners */
    for (int r = 0; r < rows - 1; r++) {
        for (int c = 0; c < cols - 1; c++) {
            float avg = (map->heights[r * cols + c]
                       + map->heights[r * cols + c + 1]
                       + map->heights[(r + 1) * cols + c]
                       + map->heights[(r + 1) * cols + c + 1]) * 0.25f;
            float t = avg / map->max_height;  /* 0..1 */
            int ci = (r * (cols - 1) + c) * 3;
            if (t < 0.3f) {
                /* Dark green — valleys */
                map->colors[ci]     = 40;
                map->colors[ci + 1] = 120;
                map->colors[ci + 2] = 40;
            } else if (t < 0.7f) {
                /* Light green — fields */
                map->colors[ci]     = 80;
                map->colors[ci + 1] = 160;
                map->colors[ci + 2] = 60;
            } else {
                /* Brown/grey — hilltops */
                map->colors[ci]     = 140;
                map->colors[ci + 1] = 110;
                map->colors[ci + 2] = 70;
            }
        }
    }

    /* Precompute vertex normals by averaging adjacent face normals */
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            vector sum = {0, 0, 0};
            int count = 0;
            /* Check all triangles that share this vertex.
               Each cell (r,c) is split into 2 tris:
                 A: (r,c), (r+1,c), (r,c+1)
                 B: (r+1,c), (r+1,c+1), (r,c+1)
               A vertex at (r,c) participates in up to 6 triangles. */
            for (int dr = -1; dr <= 0; dr++) {
                for (int dc = -1; dc <= 0; dc++) {
                    int cr = r + dr;
                    int cc = c + dc;
                    if (cr < 0 || cr >= rows - 1 || cc < 0 || cc >= cols - 1)
                        continue;
                    /* Cell (cr, cc) corners */
                    float h00 = map->heights[cr * cols + cc];
                    float h10 = map->heights[(cr + 1) * cols + cc];
                    float h01 = map->heights[cr * cols + cc + 1];
                    float h11 = map->heights[(cr + 1) * cols + cc + 1];

                    float cw = map->width / (float)(cols - 1);
                    float cd = map->depth / (float)(rows - 1);

                    /* Triangle A: (cr,cc), (cr+1,cc), (cr,cc+1) */
                    vector a0 = {cc * cw, h00, cr * cd};
                    vector a1 = {cc * cw, h10, (cr + 1) * cd};
                    vector a2 = {(cc + 1) * cw, h01, cr * cd};
                    vector fn_a = vector_normalize(vector_cross(
                        vector_sub(a1, a0), vector_sub(a2, a0)));
                    sum = vector_add(sum, fn_a);
                    count++;

                    /* Triangle B: (cr+1,cc), (cr+1,cc+1), (cr,cc+1) */
                    vector b0 = {cc * cw, h10, (cr + 1) * cd};
                    vector b1 = {(cc + 1) * cw, h11, (cr + 1) * cd};
                    vector b2 = {(cc + 1) * cw, h01, cr * cd};
                    vector fn_b = vector_normalize(vector_cross(
                        vector_sub(b1, b0), vector_sub(b2, b0)));
                    sum = vector_add(sum, fn_b);
                    count++;
                }
            }
            vector n = (count > 0) ? vector_normalize(sum) : (vector){0, 1, 0};
            int idx = (r * cols + c) * 3;
            map->normals[idx]     = n.x;
            map->normals[idx + 1] = n.y;
            map->normals[idx + 2] = n.z;
        }
    }
}
```

- [ ] **Step 2: Implement `bf_map_height_at`**

Add right after `bf_map_generate_test_terrain`:

```c
float bf_map_height_at(const bf_map *map, float x, float z) {
    if (!map->heights) return 0.0f;

    int cols = map->grid_cols;
    int rows = map->grid_rows;
    float cell_w = map->width / (float)(cols - 1);
    float cell_d = map->depth / (float)(rows - 1);

    /* Convert to grid coordinates */
    float gx = x / cell_w;
    float gz = z / cell_d;

    /* Clamp to grid bounds */
    if (gx < 0.0f) gx = 0.0f;
    if (gx > (float)(cols - 2)) gx = (float)(cols - 2);
    if (gz < 0.0f) gz = 0.0f;
    if (gz > (float)(rows - 2)) gz = (float)(rows - 2);

    int c0 = (int)floorf(gx);
    int r0 = (int)floorf(gz);
    if (c0 >= cols - 1) c0 = cols - 2;
    if (r0 >= rows - 1) r0 = rows - 2;

    float fx = gx - c0;
    float fz = gz - r0;

    /* Bilinear interpolation */
    float h00 = map->heights[r0 * cols + c0];
    float h01 = map->heights[r0 * cols + c0 + 1];
    float h10 = map->heights[(r0 + 1) * cols + c0];
    float h11 = map->heights[(r0 + 1) * cols + c0 + 1];

    float h = h00 * (1.0f - fx) * (1.0f - fz)
            + h01 * fx * (1.0f - fz)
            + h10 * (1.0f - fx) * fz
            + h11 * fx * fz;
    return h;
}
```

- [ ] **Step 3: Add `#include <float.h>` if not already present at top of `battleforge.c`**

It's already there (line 8). No change needed.

- [ ] **Step 4: Do NOT build or commit yet**

Continue to Task 6 — the `bf_render` and `main.c` still reference removed `r, g, b` fields. All battleforge changes commit together in Task 6.

---

### Task 6: Update bf_render, bf_pick, bf_tick, and bf_destroy

**Files:**
- Modify: `libs/battleforge/battleforge.c:125-132` (`bf_destroy` — free terrain arrays)
- Modify: `libs/battleforge/battleforge.c:268-284` (`bf_tick` entity movement — XZ-only)
- Modify: `libs/battleforge/battleforge.c:354-359` (`bf_render` — swap plane for heightfield)
- Modify: `libs/battleforge/battleforge.c:466-474` (`bf_pick` — use `rt_intersect_heightfield`)

- [ ] **Step 1: Free terrain data in `bf_destroy`**

In `bf_destroy` (lines 125-132), add before `free(e)`:

```c
    free(e->map.heights);
    free(e->map.colors);
    free(e->map.normals);
```

- [ ] **Step 2: Update entity movement to XZ-only in `bf_tick`**

Replace entity movement block (lines 272-283):

```c
        vector to_target = vector_sub(ent->target, ent->position);
        to_target.y = 0.0f;  /* XZ-only distance */
        float dist = vector_magnitude(to_target);
        float step = ent->speed * dt;

        if (dist <= step) {
            ent->position.x = ent->target.x;
            ent->position.z = ent->target.z;
        } else {
            vector move_dir = vector_scale(to_target, 1.0f / dist);
            ent->direction = move_dir;
            ent->position.x += move_dir.x * step;
            ent->position.z += move_dir.z * step;
        }

        /* Snap to terrain height */
        if (e->map_set && e->map.heights) {
            ent->position.y = bf_map_height_at(&e->map,
                                                ent->position.x, ent->position.z);
        }
```

- [ ] **Step 3: Replace ground plane with heightfield in `bf_render`**

Replace lines 354-359 (the ground plane block):

```c
        /* Heightfield terrain */
        if (e->map.heights) {
            rt_heightfield hf = {
                .heights = e->map.heights,
                .colors = e->map.colors,
                .normals = e->map.normals,
                .rows = e->map.grid_rows,
                .cols = e->map.grid_cols,
                .world_width = e->map.width,
                .world_depth = e->map.depth,
                .origin_x = 0.0f,
                .origin_z = 0.0f,
                .max_height = e->map.max_height
            };
            rt_scene_add_heightfield(e->scene, &hf);
        }
```

- [ ] **Step 4: Replace ground plane picking with heightfield picking in `bf_pick`**

Replace lines 466-474 (the ground plane test):

```c
    /* Test heightfield terrain */
    if (e->map_set && e->map.heights) {
        rt_heightfield hf = {
            .heights = e->map.heights,
            .colors = e->map.colors,
            .normals = e->map.normals,
            .rows = e->map.grid_rows,
            .cols = e->map.grid_cols,
            .world_width = e->map.width,
            .world_depth = e->map.depth,
            .origin_x = 0.0f,
            .origin_z = 0.0f,
            .max_height = e->map.max_height
        };
        float t;
        vector hn;
        if (rt_intersect_heightfield(&hf, origin, ray_dir, &t, &hn, NULL, NULL)) {
            if (t > 0.0f) {
                result.type = BF_PICK_GROUND;
                result.position = vector_add(origin, vector_scale(ray_dir, t));
                return result;
            }
        }
    }
```

- [ ] **Step 5: Build all battleforge changes together**

Run: `cd /home/rafa/repos/c && make 2>&1 | head -20`

Expected: Only `main.c` errors remain (referencing `.r`, `.g`, `.b` in `bf_set_map` call). All battleforge library code compiles cleanly.

- [ ] **Step 6: Commit all battleforge changes (Tasks 4 + 5 + 6)**

```bash
git add libs/battleforge/battleforge.h libs/battleforge/battleforge.c
git commit -m "feat(battleforge): add heightfield terrain with generation, snapping, and picking"
```

---

## Chunk 3: Main App Integration

### Task 7: Update main.c to use terrain

**Files:**
- Modify: `apps/battleforge/main.c:259-267` (bf_set_map call)
- Modify: `apps/battleforge/main.c:314-337` (entity creation — remove hardcoded Y=1.0)
- Modify: `apps/battleforge/main.c:394-405` (right-click move — remove hardcoded dest.y)

- [ ] **Step 1: Update `bf_set_map` call and add terrain generation**

Replace lines 259-267:

```c
    /* Set map and generate terrain */
    bf_map map = {
        .width = 100.0f,
        .depth = 100.0f,
        .grid_cols = 64,
        .grid_rows = 64,
        .max_height = 10.0f,
        .ambient = 0.15f,
        .light_dir = {1.0f, 1.0f, -1.0f},
        .light_intensity = 0.85f
    };
    bf_map_generate_test_terrain(&map);
    bf_set_map(engine, map);
```

- [ ] **Step 2: Remove hardcoded Y=1.0 from entity creation**

In entity creation commands (lines 314-337), change the position Y values from `1.0f` to `0.0f`. The entities will be snapped to terrain height by `bf_tick`:

For entity 1 (line 317): `.position = {0.0f, 0.0f, 0.0f}`
For entity 2 (line 324): `.position = {5.0f, 0.0f, -3.0f}`
For entity 3 (line 334): `.position = {-4.0f, 0.0f, 2.0f}`

- [ ] **Step 3: Remove hardcoded `dest.y = 1.0f` from right-click movement**

Replace lines 396-397:

```c
                    if (selected_id > 0 && pick.type == BF_PICK_GROUND) {
                        vector dest = pick.position;
```

Remove the line `dest.y = 1.0f;` (line 397 in original). The entity will snap to terrain height via `bf_tick`.

- [ ] **Step 4: Raise initial camera Y to see dramatic terrain**

Change `cam_y` initial value (line 356) from `8.0f` to `15.0f`:

```c
    float cam_x = 0.0f, cam_y = 15.0f, cam_z = 15.0f;
```

Also update the initial camera command (lines 348-352):

```c
    bf_command(engine, (bf_cmd){
        .type = BF_CMD_CAMERA_SET,
        .camera_set = {
            .position = {0.0f, 15.0f, 15.0f},
            .direction = {0.0f, -0.4f, -1.0f}
        }
    });
```

- [ ] **Step 5: Build the full project**

Run: `cd /home/rafa/repos/c && make`

Expected: Compiles with no errors.

- [ ] **Step 6: Run and visually verify terrain**

Run: `cd /home/rafa/repos/c && ./apps/battleforge/battleforge`

Expected:
- Rolling hills visible instead of flat green plane
- Three color bands: dark green valleys, light green fields, brown hilltops
- Directional lighting creates shading across slopes
- Entities standing on terrain surface (not floating, not clipping)
- Click-to-move places entities correctly on terrain
- Camera WASD panning works over terrain

- [ ] **Step 7: Commit**

```bash
git add apps/battleforge/main.c
git commit -m "feat(battleforge): integrate heightfield terrain in main app"
```

---

## Chunk 4: Final Verification

### Task 8: End-to-end verification and final commit

- [ ] **Step 1: Clean build from scratch**

Run: `cd /home/rafa/repos/c && make clean && make`

Expected: Full clean build with zero warnings, zero errors.

- [ ] **Step 2: Run and verify all features work**

Run: `cd /home/rafa/repos/c && ./apps/battleforge/battleforge`

Verify:
- Terrain renders with rolling hills and height-based coloring
- Phong shading creates light/shadow on slopes
- Entities stand correctly on terrain at different heights
- Left-click selects entities
- Right-click moves selected entity to terrain surface
- Camera panning with WASD, rotation with arrows
- FPS is acceptable (check window title)

- [ ] **Step 3: Final commit if any fixes were needed**

```bash
git add -A
git commit -m "fix(battleforge): terrain integration fixes"
```

Only create this commit if fixes were made in step 2.
