#include "scene_bvh.h"
#include "renderer.h"   /* RT_OBJ_KIND_* */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SBVH_LEAF_MAX 4
#define SBVH_BINS     12

/* ------------------------------------------------------------ bounds */

static void bounds_empty(float *b) {
    b[0] = b[1] = b[2] =  INFINITY;
    b[3] = b[4] = b[5] = -INFINITY;
}

static void bounds_grow(float *b, const float *o) {
    for (int i = 0; i < 3; i++) {
        if (o[i]     < b[i])     b[i]     = o[i];
        if (o[i + 3] > b[i + 3]) b[i + 3] = o[i + 3];
    }
}

static float bounds_area(const float *b) {
    float dx = b[3] - b[0], dy = b[4] - b[1], dz = b[5] - b[2];
    if (dx < 0 || dy < 0 || dz < 0) return 0.0f;
    return 2.0f * (dx * dy + dy * dz + dz * dx);
}

/* center ± ext, then padded so float error in the slab test can never
 * cull a ray the exact intersector would hit (grazing silhouettes, flat
 * discs and triangles with zero thickness along one axis). */
static void bounds_set(float *b, vector c, vector ext) {
    float e[3] = {fabsf(ext.x), fabsf(ext.y), fabsf(ext.z)};
    float p[3] = {c.x, c.y, c.z};
    for (int i = 0; i < 3; i++) {
        float mag = fabsf(p[i]) + e[i];
        float pad = 1e-4f + 1e-5f * mag;
        b[i]     = p[i] - e[i] - pad;
        b[i + 3] = p[i] + e[i] + pad;
    }
}

/* Half-extent along each world axis of a circle of radius r whose plane
 * has unit normal n: r * sqrt(1 - n_i^2). */
static vector circle_ext(vector n, float r) {
    return (vector){r * sqrtf(fmaxf(0.0f, 1.0f - n.x * n.x)),
                    r * sqrtf(fmaxf(0.0f, 1.0f - n.y * n.y)),
                    r * sqrtf(fmaxf(0.0f, 1.0f - n.z * n.z))};
}

static vector safe_unit(vector v) {
    float m = vector_magnitude(v);
    return m > 1e-12f ? vector_scale(v, 1.0f / m) : (vector){0, 1, 0};
}

static void prim_bounds(const scene *s, uint32_t ref, float *b) {
    int i = RT_SBVH_INDEX(ref);
    switch (RT_SBVH_KIND(ref)) {
    case RT_OBJ_KIND_SPHERE: {
        float r = s->spheres[i].radius;
        bounds_set(b, s->spheres[i].center, (vector){r, r, r});
        break;
    }
    case RT_OBJ_KIND_DISC: {
        const scene_disc *d = &s->discs[i];
        bounds_set(b, d->center, circle_ext(safe_unit(d->normal), d->radius));
        break;
    }
    case RT_OBJ_KIND_CYLINDER: {
        /* Open tube: two end circles swept along the axis. */
        const scene_cylinder *c = &s->cylinders[i];
        vector a = safe_unit(c->axis);
        vector ce = circle_ext(a, c->radius);
        vector ext = {fabsf(a.x) * c->half_height + ce.x,
                      fabsf(a.y) * c->half_height + ce.y,
                      fabsf(a.z) * c->half_height + ce.z};
        bounds_set(b, c->center, ext);
        break;
    }
    case RT_OBJ_KIND_CONE: {
        /* Hull of the apex point and the base circle. */
        const scene_cone *c = &s->cones[i];
        vector a = safe_unit(c->axis);
        vector base = vector_add(c->apex, vector_scale(a, c->height));
        vector ce = circle_ext(a, c->radius);
        float bb[6];
        bounds_set(bb, base, ce);
        bounds_set(b, c->apex, (vector){0, 0, 0});
        bounds_grow(b, bb);
        break;
    }
    case RT_OBJ_KIND_TORUS: {
        /* The torus intersector only reports hits inside its bounding
         * sphere (radius R + r) — and it sphere-traces with a distance-
         * dependent epsilon, so a hit can sit slightly off the tight
         * surface. Bounding the same sphere keeps the BVH exactly as
         * permissive as the intersector. */
        const scene_torus *t = &s->toruses[i];
        float R = t->major_radius + t->minor_radius;
        bounds_set(b, t->center, (vector){R, R, R});
        break;
    }
    case RT_OBJ_KIND_TRIANGLE: {
        const scene_triangle *t = &s->triangles[i];
        vector mn = {fminf(t->v0.x, fminf(t->v1.x, t->v2.x)),
                     fminf(t->v0.y, fminf(t->v1.y, t->v2.y)),
                     fminf(t->v0.z, fminf(t->v1.z, t->v2.z))};
        vector mx = {fmaxf(t->v0.x, fmaxf(t->v1.x, t->v2.x)),
                     fmaxf(t->v0.y, fmaxf(t->v1.y, t->v2.y)),
                     fmaxf(t->v0.z, fmaxf(t->v1.z, t->v2.z))};
        bounds_set(b, vector_scale(vector_add(mn, mx), 0.5f),
                   vector_scale(vector_sub(mx, mn), 0.5f));
        break;
    }
    case RT_OBJ_KIND_BOX: {
        /* OBB -> AABB: project each local half-axis onto the world axes. */
        const scene_box *x = &s->boxes[i];
        vector h = x->half_extents;
        vector ext = {
            fabsf(x->ux.x) * h.x + fabsf(x->uy.x) * h.y + fabsf(x->uz.x) * h.z,
            fabsf(x->ux.y) * h.x + fabsf(x->uy.y) * h.y + fabsf(x->uz.y) * h.z,
            fabsf(x->ux.z) * h.x + fabsf(x->uy.z) * h.y + fabsf(x->uz.z) * h.z,
        };
        bounds_set(b, x->center, ext);
        break;
    }
    default:
        bounds_empty(b);
        break;
    }
}

/* ------------------------------------------------------------- build */

static void swap_prim(rt_scene_bvh *b, int i, int j) {
    uint32_t t = b->prims[i]; b->prims[i] = b->prims[j]; b->prims[j] = t;
    float tmp[6];
    memcpy(tmp, &b->bounds[i * 6], sizeof tmp);
    memcpy(&b->bounds[i * 6], &b->bounds[j * 6], sizeof tmp);
    memcpy(&b->bounds[j * 6], tmp, sizeof tmp);
}

static float centroid(const float *pb, int axis) {
    return 0.5f * (pb[axis] + pb[axis + 3]);
}

static int build_node(rt_scene_bvh *b, int start, int count) {
    int idx = b->node_count++;
    rt_bvh_node *n = &b->nodes[idx];

    float nb[6], cb[6];
    bounds_empty(nb);
    bounds_empty(cb);
    for (int i = start; i < start + count; i++) {
        const float *pb = &b->bounds[i * 6];
        bounds_grow(nb, pb);
        float c[6] = {centroid(pb, 0), centroid(pb, 1), centroid(pb, 2),
                      centroid(pb, 0), centroid(pb, 1), centroid(pb, 2)};
        bounds_grow(cb, c);
    }
    memcpy(n->aabb_min, nb, 3 * sizeof(float));
    memcpy(n->aabb_max, nb + 3, 3 * sizeof(float));
    n->_pad = 0;

    int make_leaf = count <= 1;
    int axis = 0;
    float ext[3] = {cb[3] - cb[0], cb[4] - cb[1], cb[5] - cb[2]};
    if (ext[1] > ext[axis]) axis = 1;
    if (ext[2] > ext[axis]) axis = 2;
    if (!(ext[axis] > 1e-9f)) make_leaf = count <= SBVH_LEAF_MAX;

    int mid = -1;
    if (!make_leaf && ext[axis] > 1e-9f) {
        /* Binned SAH along the widest centroid axis. */
        int   bin_n[SBVH_BINS] = {0};
        float bin_b[SBVH_BINS][6];
        for (int k = 0; k < SBVH_BINS; k++) bounds_empty(bin_b[k]);
        float lo = cb[axis], scale = (float)SBVH_BINS / ext[axis];
        for (int i = start; i < start + count; i++) {
            int k = (int)((centroid(&b->bounds[i * 6], axis) - lo) * scale);
            if (k >= SBVH_BINS) k = SBVH_BINS - 1;
            if (k < 0) k = 0;
            bin_n[k]++;
            bounds_grow(bin_b[k], &b->bounds[i * 6]);
        }
        float left_area[SBVH_BINS], acc[6];
        int left_n[SBVH_BINS], cnt = 0;
        bounds_empty(acc);
        for (int k = 0; k < SBVH_BINS - 1; k++) {
            bounds_grow(acc, bin_b[k]);
            cnt += bin_n[k];
            left_area[k] = bounds_area(acc);
            left_n[k] = cnt;
        }
        float best_cost = INFINITY;
        int best_k = -1;
        bounds_empty(acc);
        cnt = 0;
        for (int k = SBVH_BINS - 1; k > 0; k--) {
            bounds_grow(acc, bin_b[k]);
            cnt += bin_n[k];
            int ln = left_n[k - 1];
            if (ln == 0 || cnt == 0) continue;
            float cost = left_area[k - 1] * (float)ln + bounds_area(acc) * (float)cnt;
            if (cost < best_cost) { best_cost = cost; best_k = k - 1; }
        }
        float leaf_cost = bounds_area(nb) * (float)count;
        if (best_k >= 0 && (best_cost < leaf_cost || count > SBVH_LEAF_MAX)) {
            int i = start, j = start + count - 1;
            while (i <= j) {
                int k = (int)((centroid(&b->bounds[i * 6], axis) - lo) * scale);
                if (k >= SBVH_BINS) k = SBVH_BINS - 1;
                if (k <= best_k) i++;
                else swap_prim(b, i, j--);
            }
            mid = i;
            if (mid == start || mid == start + count) mid = -1;
        } else if (count <= SBVH_LEAF_MAX) {
            make_leaf = 1;
        }
    }
    if (!make_leaf && mid < 0) mid = start + count / 2;   /* degenerate: split by order */

    if (make_leaf) {
        n->tri_start = start;
        n->tri_count = count;
        n->second_child_offset = 0;
        return idx;
    }
    n->tri_start = 0;
    n->tri_count = 0;
    build_node(b, start, mid - start);
    int right = build_node(b, mid, start + count - mid);
    b->nodes[idx].second_child_offset = right - idx;   /* `n` may be stale: index again */
    return idx;
}

static int ensure(void **p, int *cap, int need, size_t elem) {
    if (need <= *cap) return 1;
    int nc = *cap ? *cap : 64;
    while (nc < need) nc *= 2;
    void *np = realloc(*p, (size_t)nc * elem);
    if (!np) return 0;
    *p = np;
    *cap = nc;
    return 1;
}

int rt_scene_bvh_build(rt_scene_bvh *b, const scene *s) {
    b->node_count = 0;
    b->prim_count = 0;

    int total = s->sphere_count + s->disc_count + s->cylinder_count + s->cone_count +
                s->torus_count + s->triangle_count + s->box_count;
    if (total == 0) return 1;
    if (!ensure((void **)&b->prims, &b->prim_cap, total, sizeof(uint32_t)) ||
        !ensure((void **)&b->bounds, &b->bounds_cap, total * 6, sizeof(float)) ||
        !ensure((void **)&b->nodes, &b->node_cap, 2 * total, sizeof(rt_bvh_node)))
        return 0;

    int n = 0;
    for (int i = 0; i < s->sphere_count; i++)
        if (!rt_sbvh_sphere_is_linear(&s->spheres[i]))
            b->prims[n++] = RT_SBVH_REF(RT_OBJ_KIND_SPHERE, i);
    for (int i = 0; i < s->disc_count; i++)     b->prims[n++] = RT_SBVH_REF(RT_OBJ_KIND_DISC, i);
    for (int i = 0; i < s->cylinder_count; i++) b->prims[n++] = RT_SBVH_REF(RT_OBJ_KIND_CYLINDER, i);
    for (int i = 0; i < s->cone_count; i++)     b->prims[n++] = RT_SBVH_REF(RT_OBJ_KIND_CONE, i);
    for (int i = 0; i < s->torus_count; i++)    b->prims[n++] = RT_SBVH_REF(RT_OBJ_KIND_TORUS, i);
    for (int i = 0; i < s->triangle_count; i++) b->prims[n++] = RT_SBVH_REF(RT_OBJ_KIND_TRIANGLE, i);
    for (int i = 0; i < s->box_count; i++)      b->prims[n++] = RT_SBVH_REF(RT_OBJ_KIND_BOX, i);
    b->prim_count = n;
    if (n == 0) return 1;

    for (int i = 0; i < n; i++) prim_bounds(s, b->prims[i], &b->bounds[i * 6]);
    build_node(b, 0, n);
    return 1;
}

void rt_scene_bvh_free(rt_scene_bvh *b) {
    free(b->nodes);
    free(b->prims);
    free(b->bounds);
    memset(b, 0, sizeof *b);
}
