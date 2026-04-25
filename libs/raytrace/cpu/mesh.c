#include "mesh.h"
#include "cpu/bvh.h"
#include <math.h>
#include <float.h>

typedef struct {
    float t;
    float bu, bv;
    int   i0, i1, i2;
    int   have_hit;
} mesh_best;

static inline void test_triangle(vector ro, vector rd, const scene_mesh *mesh,
                                  int tri, mesh_best *best) {
    uint32_t i0 = mesh->indices[tri * 3 + 0];
    uint32_t i1 = mesh->indices[tri * 3 + 1];
    uint32_t i2 = mesh->indices[tri * 3 + 2];
    if ((int)i0 >= mesh->vertex_count ||
        (int)i1 >= mesh->vertex_count ||
        (int)i2 >= mesh->vertex_count) return;

    vector v0 = mesh->vertices[i0].position;
    vector v1 = mesh->vertices[i1].position;
    vector v2 = mesh->vertices[i2].position;

    vector e1 = vector_sub(v1, v0);
    vector e2 = vector_sub(v2, v0);
    vector pvec = vector_cross(rd, e2);
    float det = vector_dot(e1, pvec);
    if (fabsf(det) < 1e-6f) return;

    float inv_det = 1.0f / det;
    vector tvec = vector_sub(ro, v0);
    float bu = vector_dot(tvec, pvec) * inv_det;
    if (bu < 0.0f || bu > 1.0f) return;

    vector qvec = vector_cross(tvec, e1);
    float bv = vector_dot(rd, qvec) * inv_det;
    if (bv < 0.0f || bu + bv > 1.0f) return;

    float t = vector_dot(e2, qvec) * inv_det;
    if (t <= 0.0f || t >= best->t) return;

    best->t = t;
    best->bu = bu;
    best->bv = bv;
    best->i0 = (int)i0;
    best->i1 = (int)i1;
    best->i2 = (int)i2;
    best->have_hit = 1;
}

static void linear_scan(vector ro, vector rd, const scene_mesh *mesh,
                        mesh_best *best) {
    int tri_count = mesh->index_count / 3;
    for (int tri = 0; tri < tri_count; tri++) {
        test_triangle(ro, rd, mesh, tri, best);
    }
}

static void bvh_traverse(vector ro, vector rd, const scene_mesh *mesh,
                          mesh_best *best) {
    const rt_bvh_node *nodes = (const rt_bvh_node *)mesh->accel;
    if (!nodes || mesh->accel_count == 0) {
        linear_scan(ro, rd, mesh, best);
        return;
    }

    /* Inverse ray direction for the slab test. Guard against zero
     * components so the slab math doesn't NaN. */
    vector inv_rd = {
        (fabsf(rd.x) > 1e-20f) ? 1.0f / rd.x : (rd.x >= 0 ? FLT_MAX : -FLT_MAX),
        (fabsf(rd.y) > 1e-20f) ? 1.0f / rd.y : (rd.y >= 0 ? FLT_MAX : -FLT_MAX),
        (fabsf(rd.z) > 1e-20f) ? 1.0f / rd.z : (rd.z >= 0 ? FLT_MAX : -FLT_MAX),
    };

    int stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        int idx = stack[--sp];
        const rt_bvh_node *n = &nodes[idx];

        float t_enter;
        if (!rt_bvh_ray_aabb(ro, inv_rd, n->aabb_min, n->aabb_max, &t_enter)) continue;
        if (t_enter > best->t) continue;

        if (n->tri_count > 0) {
            int end = n->tri_start + n->tri_count;
            for (int tri = n->tri_start; tri < end; tri++) {
                test_triangle(ro, rd, mesh, tri, best);
            }
        } else {
            if (sp + 2 > (int)(sizeof(stack) / sizeof(stack[0]))) {
                /* Stack overflow would mean a pathologically deep BVH
                 * (>64). Fall back to linear scan for correctness. */
                linear_scan(ro, rd, mesh, best);
                return;
            }
            stack[sp++] = idx + 1;                        /* left child */
            stack[sp++] = idx + n->second_child_offset;   /* right child */
        }
    }
}

int rt_intersect_mesh(vector ro, vector rd, const scene_mesh *mesh,
                      const mat4 *world_inv, rt_mesh_hit *out) {
    if (!mesh || mesh->index_count < 3 || !mesh->indices || !mesh->vertices) {
        return 0;
    }

    /* Push the ray into mesh-local space if the caller provided the
     * mesh's inverse world transform. BVH AABBs and triangle vertices
     * stay in their stored space; only the ray moves. */
    vector ro_local = ro, rd_local = rd;
    if (world_inv) {
        ro_local = mat4_transform_point(*world_inv, ro);
        rd_local = mat4_transform_dir(*world_inv, rd);
    }

    /* Cheap outer bounding-sphere reject. Faster than an AABB slab test
     * and catches most obvious misses before we touch the BVH. */
    if (mesh->bounds_radius > 0.0f) {
        vector oc = vector_sub(ro_local, mesh->bounds_center);
        float b = vector_dot(oc, rd_local);
        /* Compare against rd_local · rd_local times r² so a non-unit
         * local rd (from a scaled inverse) still rejects correctly. */
        float rd2 = vector_dot(rd_local, rd_local);
        float r2 = mesh->bounds_radius * mesh->bounds_radius;
        float c = vector_dot(oc, oc) - r2;
        if (c > 0.0f && b > 0.0f) return 0;
        float disc = b * b - rd2 * c;
        if (disc < 0.0f) return 0;
    }

    mesh_best best = { .t = FLT_MAX };
    bvh_traverse(ro_local, rd_local, mesh, &best);
    if (!best.have_hit) return 0;

    float w0 = 1.0f - best.bu - best.bv;
    float w1 = best.bu;
    float w2 = best.bv;

    const scene_vertex *sv0 = &mesh->vertices[best.i0];
    const scene_vertex *sv1 = &mesh->vertices[best.i1];
    const scene_vertex *sv2 = &mesh->vertices[best.i2];

    vector n0 = sv0->normal, n1 = sv1->normal, n2 = sv2->normal;
    vector n = {
        n0.x * w0 + n1.x * w1 + n2.x * w2,
        n0.y * w0 + n1.y * w1 + n2.y * w2,
        n0.z * w0 + n1.z * w1 + n2.z * w2,
    };
    float nmag = vector_magnitude(n);
    if (nmag < 1e-6f) {
        vector e1 = vector_sub(sv1->position, sv0->position);
        vector e2 = vector_sub(sv2->position, sv0->position);
        n = vector_normalize(vector_cross(e1, e2));
    } else {
        n = vector_scale(n, 1.0f / nmag);
    }

    /* Bring the local-space normal back to world space using the
     * inverse-transpose rule. With the mat4_transform_normal helper
     * that's `transpose(world_inv_3x3) * n_local`, which equals
     * `world_3x3 * n_local` for pure rotation and corrects for
     * non-uniform scale. Renormalize since scale can stretch length. */
    if (world_inv) {
        n = vector_normalize(mat4_transform_normal(*world_inv, n));
    }

    out->t = best.t;
    out->normal = n;
    out->u = sv0->u * w0 + sv1->u * w1 + sv2->u * w2;
    out->v = sv0->v * w0 + sv1->v * w1 + sv2->v * w2;
    return 1;
}

void rt_mesh_build_bvh(scene_mesh *mesh) {
    rt_bvh_build(mesh);
}

void rt_scene_build_accel(scene *s) {
    if (!s) return;
    for (int i = 0; i < s->mesh_count; i++) {
        rt_bvh_build(&s->meshes[i]);
    }
}
