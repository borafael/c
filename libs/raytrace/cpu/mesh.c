#include "mesh.h"
#include <math.h>
#include <float.h>

int rt_intersect_mesh(vector ro, vector rd, const scene_mesh *mesh,
                      rt_mesh_hit *out) {
    if (!mesh || mesh->index_count < 3 || !mesh->indices || !mesh->vertices) {
        return 0;
    }

    /* Bounding-sphere quick reject: solve |ro + t*rd - center|^2 = r^2.
     * If the ray never intersects the sphere, no triangle inside it can
     * be hit either. Skipped when radius is 0 (bounds not computed). */
    if (mesh->bounds_radius > 0.0f) {
        vector oc = vector_sub(ro, mesh->bounds_center);
        float b = vector_dot(oc, rd);
        float c = vector_dot(oc, oc) - mesh->bounds_radius * mesh->bounds_radius;
        /* If origin is outside sphere (c > 0) and moving away (b > 0), miss. */
        if (c > 0.0f && b > 0.0f) return 0;
        float disc = b * b - c;
        if (disc < 0.0f) return 0;
    }

    float  best_t = FLT_MAX;
    float  best_bu = 0.0f, best_bv = 0.0f;
    int    best_i0 = 0, best_i1 = 0, best_i2 = 0;
    int    have_hit = 0;

    int tri_count = mesh->index_count / 3;
    for (int tri = 0; tri < tri_count; tri++) {
        uint32_t i0 = mesh->indices[tri * 3 + 0];
        uint32_t i1 = mesh->indices[tri * 3 + 1];
        uint32_t i2 = mesh->indices[tri * 3 + 2];
        if ((int)i0 >= mesh->vertex_count ||
            (int)i1 >= mesh->vertex_count ||
            (int)i2 >= mesh->vertex_count) {
            continue;
        }

        vector v0 = mesh->vertices[i0].position;
        vector v1 = mesh->vertices[i1].position;
        vector v2 = mesh->vertices[i2].position;

        vector e1 = vector_sub(v1, v0);
        vector e2 = vector_sub(v2, v0);
        vector pvec = vector_cross(rd, e2);
        float  det = vector_dot(e1, pvec);
        if (fabsf(det) < 1e-6f) continue;

        float inv_det = 1.0f / det;
        vector tvec = vector_sub(ro, v0);
        float bu = vector_dot(tvec, pvec) * inv_det;
        if (bu < 0.0f || bu > 1.0f) continue;

        vector qvec = vector_cross(tvec, e1);
        float bv = vector_dot(rd, qvec) * inv_det;
        if (bv < 0.0f || bu + bv > 1.0f) continue;

        float t = vector_dot(e2, qvec) * inv_det;
        if (t <= 0.0f || t >= best_t) continue;

        best_t  = t;
        best_bu = bu;
        best_bv = bv;
        best_i0 = (int)i0;
        best_i1 = (int)i1;
        best_i2 = (int)i2;
        have_hit = 1;
    }

    if (!have_hit) return 0;

    float w0 = 1.0f - best_bu - best_bv;
    float w1 = best_bu;
    float w2 = best_bv;

    const scene_vertex *sv0 = &mesh->vertices[best_i0];
    const scene_vertex *sv1 = &mesh->vertices[best_i1];
    const scene_vertex *sv2 = &mesh->vertices[best_i2];

    vector n0 = sv0->normal, n1 = sv1->normal, n2 = sv2->normal;
    vector n = {
        n0.x * w0 + n1.x * w1 + n2.x * w2,
        n0.y * w0 + n1.y * w1 + n2.y * w2,
        n0.z * w0 + n1.z * w1 + n2.z * w2,
    };
    float nmag = vector_magnitude(n);
    if (nmag < 1e-6f) {
        /* Vertex normals were zero or cancelled — fall back to face normal. */
        vector e1 = vector_sub(sv1->position, sv0->position);
        vector e2 = vector_sub(sv2->position, sv0->position);
        n = vector_normalize(vector_cross(e1, e2));
    } else {
        n = vector_scale(n, 1.0f / nmag);
    }

    out->t = best_t;
    out->normal = n;
    out->u = sv0->u * w0 + sv1->u * w1 + sv2->u * w2;
    out->v = sv0->v * w0 + sv1->v * w1 + sv2->v * w2;
    return 1;
}
