#include "bvh.h"
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

#define RT_BVH_LEAF_MAX 4

/* Workspace passed down the recursive build. */
typedef struct {
    scene_mesh *mesh;
    vector     *centroids;      /* one per triangle, indexed by tri number */
    int        *tri_order;      /* permutation of [0..tri_count) */
    rt_bvh_node *nodes;
    int         node_count;
} build_ctx;

static inline float axis_val(vector v, int axis) {
    if (axis == 0) return v.x;
    if (axis == 1) return v.y;
    return v.z;
}

static void aabb_for_range(build_ctx *c, int first, int count,
                           float out_min[3], float out_max[3]) {
    out_min[0] = out_min[1] = out_min[2] =  FLT_MAX;
    out_max[0] = out_max[1] = out_max[2] = -FLT_MAX;
    for (int i = 0; i < count; i++) {
        int tri = c->tri_order[first + i];
        for (int v = 0; v < 3; v++) {
            uint32_t vi = c->mesh->indices[tri * 3 + v];
            vector p = c->mesh->vertices[vi].position;
            if (p.x < out_min[0]) out_min[0] = p.x;
            if (p.y < out_min[1]) out_min[1] = p.y;
            if (p.z < out_min[2]) out_min[2] = p.z;
            if (p.x > out_max[0]) out_max[0] = p.x;
            if (p.y > out_max[1]) out_max[1] = p.y;
            if (p.z > out_max[2]) out_max[2] = p.z;
        }
    }
}

static int build_recursive(build_ctx *c, int first, int count) {
    int my_idx = c->node_count++;
    rt_bvh_node *node = &c->nodes[my_idx];

    aabb_for_range(c, first, count, node->aabb_min, node->aabb_max);
    node->_pad = 0;

    if (count <= RT_BVH_LEAF_MAX) {
        node->tri_start = first;
        node->tri_count = count;
        node->second_child_offset = 0;
        return my_idx;
    }

    float ex = node->aabb_max[0] - node->aabb_min[0];
    float ey = node->aabb_max[1] - node->aabb_min[1];
    float ez = node->aabb_max[2] - node->aabb_min[2];
    int axis = 0; float extent = ex;
    if (ey > extent) { axis = 1; extent = ey; }
    if (ez > extent) { axis = 2; extent = ez; }

    if (extent < 1e-6f) {
        /* Degenerate: make a leaf even if it exceeds LEAF_MAX. */
        node->tri_start = first;
        node->tri_count = count;
        node->second_child_offset = 0;
        return my_idx;
    }

    float mid = 0.5f * (node->aabb_min[axis] + node->aabb_max[axis]);

    int lo = first;
    int hi = first + count - 1;
    while (lo <= hi) {
        float v = axis_val(c->centroids[c->tri_order[lo]], axis);
        if (v < mid) {
            lo++;
        } else {
            int tmp = c->tri_order[lo];
            c->tri_order[lo] = c->tri_order[hi];
            c->tri_order[hi] = tmp;
            hi--;
        }
    }
    int left_count = lo - first;
    if (left_count == 0 || left_count == count) {
        /* Partition degenerate (e.g., all centroids on midpoint). Split
         * down the middle deterministically. */
        left_count = count / 2;
    }

    node->tri_start = 0;
    node->tri_count = 0;  /* internal */
    /* Left child goes at node+1 (DFS). Fill after recursion because
     * recursion may realloc/shift unrelated fields — but we know
     * node_count grows monotonically, so the pointer is stable after
     * the enclosing malloc. */
    int left_child  = build_recursive(c, first, left_count);
    (void)left_child;  /* Always equals my_idx + 1 by construction. */
    int right_child = build_recursive(c, first + left_count, count - left_count);
    c->nodes[my_idx].second_child_offset = right_child - my_idx;
    return my_idx;
}

void rt_bvh_build(scene_mesh *mesh) {
    if (!mesh) return;
    free(mesh->accel);
    mesh->accel = NULL;
    mesh->accel_count = 0;

    int tri_count = (mesh->index_count >= 3) ? (mesh->index_count / 3) : 0;
    if (tri_count == 0 || !mesh->indices || !mesh->vertices) return;

    vector *centroids = malloc(sizeof(vector) * (size_t)tri_count);
    int    *tri_order = malloc(sizeof(int)    * (size_t)tri_count);
    rt_bvh_node *nodes = malloc(sizeof(rt_bvh_node) * (size_t)(2 * tri_count));
    if (!centroids || !tri_order || !nodes) {
        free(centroids); free(tri_order); free(nodes);
        return;
    }

    for (int i = 0; i < tri_count; i++) {
        uint32_t i0 = mesh->indices[i * 3 + 0];
        uint32_t i1 = mesh->indices[i * 3 + 1];
        uint32_t i2 = mesh->indices[i * 3 + 2];
        vector a = mesh->vertices[i0].position;
        vector b = mesh->vertices[i1].position;
        vector c = mesh->vertices[i2].position;
        centroids[i] = (vector){
            (a.x + b.x + c.x) * (1.0f / 3.0f),
            (a.y + b.y + c.y) * (1.0f / 3.0f),
            (a.z + b.z + c.z) * (1.0f / 3.0f),
        };
        tri_order[i] = i;
    }

    build_ctx ctx = {
        .mesh = mesh,
        .centroids = centroids,
        .tri_order = tri_order,
        .nodes = nodes,
        .node_count = 0,
    };
    build_recursive(&ctx, 0, tri_count);

    /* Reorder mesh->indices so leaves can address contiguous ranges. */
    uint32_t *new_indices = malloc(sizeof(uint32_t) * (size_t)mesh->index_count);
    if (!new_indices) {
        free(centroids); free(tri_order); free(nodes);
        return;
    }
    for (int i = 0; i < tri_count; i++) {
        int src = tri_order[i];
        new_indices[i * 3 + 0] = mesh->indices[src * 3 + 0];
        new_indices[i * 3 + 1] = mesh->indices[src * 3 + 1];
        new_indices[i * 3 + 2] = mesh->indices[src * 3 + 2];
    }
    free(mesh->indices);
    mesh->indices = new_indices;

    free(centroids);
    free(tri_order);

    /* Shrink to fit. */
    rt_bvh_node *shrunk = realloc(nodes, sizeof(rt_bvh_node) * (size_t)ctx.node_count);
    mesh->accel = shrunk ? shrunk : nodes;
    mesh->accel_count = ctx.node_count;
}

int rt_bvh_ray_aabb(vector ro, vector inv_rd,
                    const float amin[3], const float amax[3],
                    float *t_enter) {
    float tx1 = (amin[0] - ro.x) * inv_rd.x;
    float tx2 = (amax[0] - ro.x) * inv_rd.x;
    float tmin = fminf(tx1, tx2);
    float tmax = fmaxf(tx1, tx2);

    float ty1 = (amin[1] - ro.y) * inv_rd.y;
    float ty2 = (amax[1] - ro.y) * inv_rd.y;
    tmin = fmaxf(tmin, fminf(ty1, ty2));
    tmax = fminf(tmax, fmaxf(ty1, ty2));

    float tz1 = (amin[2] - ro.z) * inv_rd.z;
    float tz2 = (amax[2] - ro.z) * inv_rd.z;
    tmin = fmaxf(tmin, fminf(tz1, tz2));
    tmax = fminf(tmax, fmaxf(tz1, tz2));

    if (tmax < 0.0f || tmin > tmax) return 0;
    *t_enter = tmin > 0.0f ? tmin : 0.0f;
    return 1;
}
