#include "render_chunk.h"
#include "sphere.h"
#include "plane.h"
#include "disc.h"
#include "cylinder.h"
#include "cone.h"
#include "torus.h"
#include "triangle.h"
#include "box.h"
#include "sprite.h"
#include "heightfield.h"
#include "mesh.h"
#include <math.h>
#include <float.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline void tangent_basis(vector n, vector *t, vector *b) {
    vector up = (fabsf(n.y) < 0.999f) ? (vector){0,1,0} : (vector){1,0,0};
    *t = vector_normalize(vector_cross(up, n));
    *b = vector_cross(n, *t);
}

static inline void uv_sphere(vector hp, vector center, float *u, float *v) {
    vector n = vector_normalize(vector_sub(hp, center));
    *u = atan2f(n.z, n.x) / (2.0f * (float)M_PI) + 0.5f;
    float ny = n.y < -1.0f ? -1.0f : (n.y > 1.0f ? 1.0f : n.y);
    *v = acosf(ny) / (float)M_PI;
}

static inline void uv_planar(vector hp, vector anchor, vector normal,
                             float *u, float *v) {
    vector t, b;
    tangent_basis(normal, &t, &b);
    vector d = vector_sub(hp, anchor);
    *u = vector_dot(d, t);
    *v = vector_dot(d, b);
}

static inline void uv_cylinder(vector hp, const scene_cylinder *cyl,
                               float *u, float *v) {
    vector axis = vector_normalize(cyl->axis);
    vector ohp = vector_sub(hp, cyl->center);
    float h = vector_dot(ohp, axis);
    vector radial = vector_sub(ohp, vector_scale(axis, h));
    vector tan_a, tan_b;
    tangent_basis(axis, &tan_a, &tan_b);
    float x = vector_dot(radial, tan_a);
    float z = vector_dot(radial, tan_b);
    *u = atan2f(z, x) / (2.0f * (float)M_PI) + 0.5f;
    *v = (h + cyl->half_height) / (2.0f * cyl->half_height);
}

static inline void uv_cone(vector hp, const scene_cone *cone,
                           float *u, float *v) {
    vector cp = vector_sub(hp, cone->apex);
    float h = vector_dot(cp, cone->axis);
    vector radial = vector_sub(cp, vector_scale(cone->axis, h));
    vector tan_a, tan_b;
    tangent_basis(cone->axis, &tan_a, &tan_b);
    float x = vector_dot(radial, tan_a);
    float z = vector_dot(radial, tan_b);
    *u = atan2f(z, x) / (2.0f * (float)M_PI) + 0.5f;
    *v = (cone->height > 0.0f) ? (h / cone->height) : 0.0f;
}

static inline void uv_torus(vector hp, const scene_torus *torus,
                            float *u, float *v) {
    vector cp = vector_sub(hp, torus->center);
    float ax = vector_dot(cp, torus->axis);
    vector radial = vector_sub(cp, vector_scale(torus->axis, ax));
    vector tan_a, tan_b;
    tangent_basis(torus->axis, &tan_a, &tan_b);
    float x = vector_dot(radial, tan_a);
    float z = vector_dot(radial, tan_b);
    /* u: angle around the central axis. v: angle around the tube. */
    *u = atan2f(z, x) / (2.0f * (float)M_PI) + 0.5f;
    float radial_len = sqrtf(x*x + z*z);
    *v = atan2f(ax, radial_len - torus->major_radius)
         / (2.0f * (float)M_PI) + 0.5f;
}

/* Procedural-noise helpers (must match the GLSL versions in
 * libs/raytrace/opengl/renderer.c for backend parity). */

static inline uint32_t noise_hash(int x, int y, int z) {
    uint32_t n = ((uint32_t)x * 73856093U)
               ^ ((uint32_t)y * 19349663U)
               ^ ((uint32_t)z * 83492791U);
    n *= 2654435761U;
    n ^= n >> 13;
    n *= 2654435761U;
    return n;
}

static inline float noise_rand(int x, int y, int z) {
    return (float)(noise_hash(x, y, z) & 0xFFFFFFU) / 16777216.0f;
}

static inline float noise_smooth(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static inline float noise_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float noise_smoothstep(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float noise3d(vector p) {
    int ix = (int)floorf(p.x);
    int iy = (int)floorf(p.y);
    int iz = (int)floorf(p.z);
    float fx = p.x - (float)ix;
    float fy = p.y - (float)iy;
    float fz = p.z - (float)iz;
    float u = noise_smooth(fx);
    float v = noise_smooth(fy);
    float w = noise_smooth(fz);

    float n000 = noise_rand(ix,     iy,     iz);
    float n100 = noise_rand(ix + 1, iy,     iz);
    float n010 = noise_rand(ix,     iy + 1, iz);
    float n110 = noise_rand(ix + 1, iy + 1, iz);
    float n001 = noise_rand(ix,     iy,     iz + 1);
    float n101 = noise_rand(ix + 1, iy,     iz + 1);
    float n011 = noise_rand(ix,     iy + 1, iz + 1);
    float n111 = noise_rand(ix + 1, iy + 1, iz + 1);

    float nx00 = noise_lerp(n000, n100, u);
    float nx10 = noise_lerp(n010, n110, u);
    float nx01 = noise_lerp(n001, n101, u);
    float nx11 = noise_lerp(n011, n111, u);
    float nxy0 = noise_lerp(nx00, nx10, v);
    float nxy1 = noise_lerp(nx01, nx11, v);
    return noise_lerp(nxy0, nxy1, w);
}

static void voronoi3d(vector p, float *f1_out, float *f2_out) {
    int bx = (int)floorf(p.x);
    int by = (int)floorf(p.y);
    int bz = (int)floorf(p.z);
    float f1 = 1e20f, f2 = 1e20f;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int cx = bx + dx, cy = by + dy, cz = bz + dz;
                float ox = (float)cx + noise_rand(cx,        cy,        cz);
                float oy = (float)cy + noise_rand(cx + 1013, cy + 1013, cz + 1013);
                float oz = (float)cz + noise_rand(cx + 2027, cy + 2027, cz + 2027);
                float rx = ox - p.x, ry = oy - p.y, rz = oz - p.z;
                float d = sqrtf(rx*rx + ry*ry + rz*rz);
                if (d < f1)      { f2 = f1; f1 = d; }
                else if (d < f2) { f2 = d; }
            }
        }
    }
    *f1_out = f1;
    *f2_out = f2;
}

static float turbulence(vector p, int octaves) {
    float total = 0.0f;
    float amp = 1.0f;
    float sum = 0.0f;
    for (int i = 0; i < octaves; i++) {
        float f = (float)(1 << i);
        vector sp = { p.x * f, p.y * f, p.z * f };
        total += amp * noise3d(sp);
        sum += amp;
        amp *= 0.5f;
    }
    return total / sum;
}

static inline scene_color color_lerp(scene_color a, scene_color b, float t) {
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    scene_color c;
    c.r = (uint8_t)((float)a.r + ((float)b.r - (float)a.r) * t);
    c.g = (uint8_t)((float)a.g + ((float)b.g - (float)a.g) * t);
    c.b = (uint8_t)((float)a.b + ((float)b.b - (float)a.b) * t);
    return c;
}

static inline scene_color material_sample(const scene_material *m,
                                       const scene_texture *textures,
                                       vector p, float u, float v) {
    if (m->tex_kind == SCENE_TEX_CHECKER) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        /* Bias by a tiny epsilon so that tile boundaries (especially hits
         * on an axis-aligned plane where one coordinate is algebraically
         * zero) don't flip parity from float rounding noise. */
        const float eps = 1e-4f;
        int ix = (int)floorf(p.x / s + eps);
        int iy = (int)floorf(p.y / s + eps);
        int iz = (int)floorf(p.z / s + eps);
        return ((ix + iy + iz) & 1) ? m->albedo2 : m->albedo;
    }
    if (m->tex_kind == SCENE_TEX_IMAGE && m->tex_index >= 0) {
        const scene_texture *t = &textures[m->tex_index];
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        float uu = u / s; uu -= floorf(uu);
        float vv = v / s; vv -= floorf(vv);
        int ix = (int)(uu * (float)t->width);
        int iy = (int)(vv * (float)t->height);
        if (ix < 0) ix = 0; else if (ix >= t->width)  ix = t->width  - 1;
        if (iy < 0) iy = 0; else if (iy >= t->height) iy = t->height - 1;
        uint32_t pixel = t->pixels[iy * t->width + ix];
        scene_color c;
        c.r = (pixel >> 16) & 0xFF;
        c.g = (pixel >>  8) & 0xFF;
        c.b =  pixel        & 0xFF;
        return c;
    }
    if (m->tex_kind == SCENE_TEX_GRADIENT) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        float t = p.y / s;
        return color_lerp(m->albedo, m->albedo2, t);
    }
    if (m->tex_kind == SCENE_TEX_NOISE) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        vector np = { p.x / s, p.y / s, p.z / s };
        float t = noise3d(np);
        return color_lerp(m->albedo, m->albedo2, t);
    }
    if (m->tex_kind == SCENE_TEX_WOOD) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        vector tp = { p.x * 0.5f, p.y * 0.5f, p.z * 0.5f };
        float turb = turbulence(tp, 4) * 0.8f;
        float r = sqrtf(p.x * p.x + p.z * p.z) / s;
        float rings = 0.5f + 0.5f * sinf((r + turb) * 6.2831853f);
        float t = rings * rings; /* sharpen dark/light contrast */
        return color_lerp(m->albedo, m->albedo2, t);
    }
    if (m->tex_kind == SCENE_TEX_MARBLE) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        float turb = turbulence(p, 5) * 5.0f;
        float t = 0.5f + 0.5f * sinf(p.x / s + turb);
        t = t * t * t * t; /* narrow, bright veins */
        return color_lerp(m->albedo, m->albedo2, t);
    }
    if (m->tex_kind == SCENE_TEX_CELLS) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        vector np = { p.x / s, p.y / s, p.z / s };
        float f1, f2;
        voronoi3d(np, &f1, &f2);
        return color_lerp(m->albedo, m->albedo2, f1);
    }
    if (m->tex_kind == SCENE_TEX_CRACKS) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        vector np = { p.x / s, p.y / s, p.z / s };
        float f1, f2;
        voronoi3d(np, &f1, &f2);
        float edge = f2 - f1;
        float e = edge / 0.12f;
        if (e < 0.0f) e = 0.0f; else if (e > 1.0f) e = 1.0f;
        float t = 1.0f - e * e * (3.0f - 2.0f * e); /* 0 inside, 1 at edge */
        return color_lerp(m->albedo, m->albedo2, t);
    }
    if (m->tex_kind == SCENE_TEX_STRIPES) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        int ix = (int)floorf(p.x / s + 1e-4f);
        float t = (ix & 1) ? 1.0f : 0.0f;
        return color_lerp(m->albedo, m->albedo2, t);
    }
    if (m->tex_kind == SCENE_TEX_DOTS) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        float ux = p.x / s; float uz = p.z / s;
        float lx = ux - floorf(ux) - 0.5f;
        float lz = uz - floorf(uz) - 0.5f;
        float d = sqrtf(lx * lx + lz * lz);
        float t = 1.0f - noise_smoothstep(0.26f, 0.30f, d);
        return color_lerp(m->albedo, m->albedo2, t);
    }
    if (m->tex_kind == SCENE_TEX_BRICKS) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        float bz = p.z / s;
        int row = (int)floorf(bz);
        float offset = (row & 1) ? 0.5f : 0.0f;
        float bx = p.x / (2.0f * s) + offset;
        float lx = bx - floorf(bx);
        float lz = bz - floorf(bz);
        float mortar = 0.04f;
        int in_mortar = (lx < mortar) || (lx > 1.0f - mortar)
                     || (lz < mortar) || (lz > 1.0f - mortar);
        float t = in_mortar ? 1.0f : 0.0f;
        return color_lerp(m->albedo, m->albedo2, t);
    }
    if (m->tex_kind == SCENE_TEX_CLOUDS) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        vector np = { p.x / s, p.y / s, p.z / s };
        float t = turbulence(np, 4);
        t = noise_smoothstep(0.40f, 0.70f, t);
        return color_lerp(m->albedo, m->albedo2, t);
    }
    if (m->tex_kind == SCENE_TEX_SPOTS) {
        float s = m->tex_scale > 0.0f ? m->tex_scale : 1.0f;
        vector np = { p.x / s, p.y / s, p.z / s };
        float n = noise3d(np);
        float t = noise_smoothstep(0.55f, 0.60f, n);
        return color_lerp(m->albedo, m->albedo2, t);
    }
    return m->albedo;
}

#define RT_MAX_BOUNCES 4
#define RT_REFLECT_EPSILON 1e-4f

/* G-buffer object-id encoding. The high byte is the primitive kind, the
 * low 24 bits are the per-kind array index. 0 is reserved for "sky" so a
 * miss can be detected with object_id == 0. Edge-detection in the comic
 * pass only compares ids for inequality; no consumer should depend on
 * the bit layout. */
#define RT_OBJ_KIND_SKY         0
#define RT_OBJ_KIND_SPHERE      1
#define RT_OBJ_KIND_PLANE       2
#define RT_OBJ_KIND_DISC        3
#define RT_OBJ_KIND_CYLINDER    4
#define RT_OBJ_KIND_TRIANGLE    5
#define RT_OBJ_KIND_MESH        6
#define RT_OBJ_KIND_BOX         7
#define RT_OBJ_KIND_SPRITE      8
#define RT_OBJ_KIND_HEIGHTFIELD 9
#define RT_OBJ_KIND_CONE       10
#define RT_OBJ_KIND_TORUS      11
#define RT_OBJ_ID(kind, index) \
    (((uint32_t)(kind) << 24) | ((uint32_t)(index) & 0x00FFFFFFu))

typedef struct {
    int hit;
    vector point;
    vector normal;
    vector entry_center;  /* primitive's anchor for portal entry-frame math
                           * (e.g. disc.center). Only meaningful when
                           * portal_index >= 0; otherwise ignored. */
    vector entry_normal;  /* primitive's AUTHORED normal — not flipped to
                           * face the camera the way `normal` is. Used as
                           * the portal entry frame's normal so that
                           * back-side hits naturally produce back-side
                           * exits. Only meaningful when portal_index >= 0. */
    float  entry_u, entry_v;  /* normalized parametric coords of the hit
                               * on the entry primitive, range [-1, 1]
                               * centered. Disc: offset / radius. Sphere:
                               * (phi/PI, 2*theta/PI - 1). Used by PARAMETRIC
                               * portals to look up the matching point on the
                               * partner. Only meaningful when portal_index
                               * >= 0 and the entry primitive populates it. */
    scene_color albedo;
    float reflectivity;
    int unlit;
    int portal_index;   /* -1 = not a portal; >= 0 indexes scene->portals */
    float distance;     /* closest_t at the primary hit; valid iff hit == 1 */
    uint32_t object_id; /* RT_OBJ_ID(kind, index); 0 on miss */
} hit_info;

/* Forward portal map P_AB(p) as a mat4: decomposes (p - entry.center)
 * in entry's tangent basis, flips the normal-axis component, and
 * recomposes in exit's tangent basis around exit.center.
 *
 *   M_linear = xtu_exit * etu_entry^T + xtv_exit * etv_entry^T
 *            - n_exit  * n_entry^T
 *   T        = exit.center - M_linear * entry.center
 *
 * Used by the mesh portal-traversal path: passing P_BA = forward(exit,
 * entry) as `world_inv` to rt_intersect_mesh pre-transforms the world
 * ray into the original mesh's frame, so the existing BVH traversal
 * intersects against unmodified vertex data — no per-frame vertex copy
 * needed. */
static inline mat4 build_portal_forward_map(const scene_disc *entry,
                                             const scene_disc *exit) {
    vector etu_e, etv_e;
    tangent_basis(entry->normal, &etu_e, &etv_e);
    vector xtu_x, xtv_x;
    tangent_basis(exit->normal, &xtu_x, &xtv_x);

    mat4 P = {0};
    P.m[15] = 1.0f;

    /* Linear part by row-col, expressed as outer-product sum. */
    P.m[ 0] = xtu_x.x * etu_e.x + xtv_x.x * etv_e.x - exit->normal.x * entry->normal.x;
    P.m[ 1] = xtu_x.x * etu_e.y + xtv_x.x * etv_e.y - exit->normal.x * entry->normal.y;
    P.m[ 2] = xtu_x.x * etu_e.z + xtv_x.x * etv_e.z - exit->normal.x * entry->normal.z;
    P.m[ 4] = xtu_x.y * etu_e.x + xtv_x.y * etv_e.x - exit->normal.y * entry->normal.x;
    P.m[ 5] = xtu_x.y * etu_e.y + xtv_x.y * etv_e.y - exit->normal.y * entry->normal.y;
    P.m[ 6] = xtu_x.y * etu_e.z + xtv_x.y * etv_e.z - exit->normal.y * entry->normal.z;
    P.m[ 8] = xtu_x.z * etu_e.x + xtv_x.z * etv_e.x - exit->normal.z * entry->normal.x;
    P.m[ 9] = xtu_x.z * etu_e.y + xtv_x.z * etv_e.y - exit->normal.z * entry->normal.y;
    P.m[10] = xtu_x.z * etu_e.z + xtv_x.z * etv_e.z - exit->normal.z * entry->normal.z;

    /* Translation: exit.center - M_linear * entry.center. */
    vector m_ec = mat4_transform_dir(P, entry->center);
    P.m[ 3] = exit->center.x - m_ec.x;
    P.m[ 7] = exit->center.y - m_ec.y;
    P.m[11] = exit->center.z - m_ec.z;
    return P;
}

/* Given an entry disc that carries a paired-rigid disc->disc portal,
 * return the partner (exit) disc. NULL on any kind of misauthored
 * scene — caller should treat as "no virtual copy for this slot." */
static inline const scene_disc *partner_disc_for_entry(
    const scene *sc, const scene_disc *entry_disc) {
    int mat_idx = entry_disc->material;
    if (mat_idx < 0 || mat_idx >= sc->material_count) return NULL;
    int portal_idx = sc->materials[mat_idx].portal_index;
    if (portal_idx < 0 || portal_idx >= sc->portal_count) return NULL;
    const scene_portal *pp = &sc->portals[portal_idx];
    if (pp->kind != SCENE_PORTAL_PAIRED_RIGID) return NULL;
    if (pp->partner_kind != SCENE_PRIM_DISC) return NULL;
    if (pp->partner_index < 0 ||
        pp->partner_index >= sc->disc_count) return NULL;
    return &sc->discs[pp->partner_index];
}

/* Fill a hit_info from a sphere intersection. Shared between the "normal"
 * sphere hit and the portal-traversal "virtual copy" hit — both have the
 * same fill logic, only the sphere data differs (the virtual copy passes
 * a sphere with a transformed center). `sphere_idx` is the index into
 * scene->spheres of the ORIGINAL sphere for object_id continuity (the
 * virtual copy shares the original's id so the G-buffer doesn't see a
 * seam). */
static inline void fill_sphere_hit(hit_info *h, const scene *sc,
                                    const scene_sphere *sph, int sphere_idx,
                                    vector hp, float t) {
    h->point  = hp;
    h->normal = rt_normal_sphere(hp, sph);
    float u, v;
    uv_sphere(hp, sph->center, &u, &v);
    const scene_material *m = &sc->materials[sph->material];
    h->albedo       = material_sample(m, sc->textures, hp, u, v);
    h->reflectivity = m->reflectivity;
    h->unlit        = m->unlit;
    h->portal_index = m->portal_index;
    h->entry_center = sph->center;
    h->entry_normal = h->normal;
    {
        vector dA = h->normal;
        float ny = dA.y; if (ny < -1.0f) ny = -1.0f; if (ny > 1.0f) ny = 1.0f;
        float theta = acosf(ny);
        float phi   = atan2f(dA.z, dA.x);
        h->entry_u = phi   / (float)M_PI;
        h->entry_v = 2.0f * theta / (float)M_PI - 1.0f;
    }
    h->hit       = 1;
    h->distance  = t;
    h->object_id = RT_OBJ_ID(RT_OBJ_KIND_SPHERE, sphere_idx);
}

static hit_info closest_hit(vector ro, vector rd, const scene *scene,
                            const mat4 *mesh_world_inv,
                            vector camera_origin) {
    hit_info h = {0};
    h.portal_index = -1;
    float closest_t = FLT_MAX;

    for (int i = 0; i < scene->sphere_count; i++) {
        const scene_sphere *sph = &scene->spheres[i];

        /* Portal-traversal: each non-zero entry in sph->portal_disc1 is
         * a paired-rigid disc portal this sphere is straddling. The
         * renderer:
         *   - Clips the original to the INTERSECTION of every tagged
         *     portal's front half-space (rejects any hit that lies
         *     behind ANY tagged portal).
         *   - Emits one virtual copy at each tagged portal's partner
         *     (clipped to the partner's front half-space).
         * The "behind portal X AND behind portal Y" region is rendered
         * at both X's and Y's virtual copies — double-counted. That's the
         * simple per-portal rule; step 5 (recursive straddling) would
         * de-duplicate. */

        /* --- Original hit, clipped against all tagged portals --- */
        float t = rt_intersect_sphere(ro, rd, sph);
        if (t > 0.0f && t < closest_t) {
            vector hp = vector_add(ro, vector_scale(rd, t));
            int keep = 1;
            for (int p = 0;
                 p < SCENE_MAX_PORTAL_TRAVERSALS && keep; p++) {
                int trav = sph->portal_disc1[p] - 1;
                if (trav < 0 || trav >= scene->disc_count) continue;
                const scene_disc *d = &scene->discs[trav];
                if (vector_dot(vector_sub(hp, d->center), d->normal) < 0.0f)
                    keep = 0;
            }
            if (keep) {
                closest_t = t;
                fill_sphere_hit(&h, scene, sph, i, hp, t);
            }
        }

        /* --- One virtual copy per tagged portal --- */
        for (int p = 0; p < SCENE_MAX_PORTAL_TRAVERSALS; p++) {
            int trav = sph->portal_disc1[p] - 1;
            if (trav < 0 || trav >= scene->disc_count) continue;
            const scene_disc *entry_disc = &scene->discs[trav];
            const scene_disc *exit_disc =
                partner_disc_for_entry(scene, entry_disc);
            if (!exit_disc) continue;   /* misauthored slot — skip */

            /* Rigid-disc portal map: tangent components carry across
             * unchanged, normal-axis flips (so "behind entry" becomes
             * "in front of exit"). */
            vector etu_A, etv_A;
            tangent_basis(entry_disc->normal, &etu_A, &etv_A);
            vector off = vector_sub(sph->center, entry_disc->center);
            float ou = vector_dot(off, etu_A);
            float ov = vector_dot(off, etv_A);
            float ow = vector_dot(off, entry_disc->normal);

            vector xtu_B, xtv_B;
            tangent_basis(exit_disc->normal, &xtu_B, &xtv_B);
            vector vcenter = vector_add(exit_disc->center,
                vector_add(
                    vector_add(vector_scale(xtu_B, ou),
                               vector_scale(xtv_B, ov)),
                    vector_scale(exit_disc->normal, -ow)));

            scene_sphere virt = {
                .center   = vcenter,
                .radius   = sph->radius,
                .material = sph->material,
                /* portal_disc1[*] all zero by zero-init — virtual is plain */
            };
            float tv = rt_intersect_sphere(ro, rd, &virt);
            if (tv > 0.0f && tv < closest_t) {
                vector hpv = vector_add(ro, vector_scale(rd, tv));
                float depth = vector_dot(vector_sub(hpv, exit_disc->center),
                                          exit_disc->normal);
                if (depth >= 0.0f) {
                    closest_t = tv;
                    fill_sphere_hit(&h, scene, &virt, i, hpv, tv);
                }
            }
        }
    }

    for (int i = 0; i < scene->plane_count; i++) {
        float t = rt_intersect_plane(ro, rd, &scene->planes[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_plane(&scene->planes[i]);
            float u, v;
            uv_planar(hp, scene->planes[i].point, h.normal, &u, &v);
            const scene_material *m = &scene->materials[scene->planes[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.unlit = m->unlit;
            h.portal_index = m->portal_index;
            h.hit = 1;
            h.distance = t;
            h.object_id = RT_OBJ_ID(RT_OBJ_KIND_PLANE, i);
        }
    }

    for (int i = 0; i < scene->disc_count; i++) {
        float t = rt_intersect_disc(ro, rd, &scene->discs[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            vector authored_n = rt_normal_disc(&scene->discs[i]);
            h.normal = authored_n;
            /* Discs are double-sided: flip the authored normal to face
             * the camera so the back side shades and reflects. */
            if (vector_dot(h.normal, rd) > 0.0f)
                h.normal = vector_scale(h.normal, -1.0f);
            float u, v;
            uv_planar(hp, scene->discs[i].center, h.normal, &u, &v);
            const scene_material *m = &scene->materials[scene->discs[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.unlit = m->unlit;
            h.portal_index = m->portal_index;
            h.entry_center = scene->discs[i].center;
            h.entry_normal = authored_n;
            {
                /* Normalized portal UV: project (hit - center) onto the
                 * disc's tangent basis, divide by radius. Range [-1, 1]
                 * within the disc; matches the convention used by
                 * inverse_disc() in the PARAMETRIC dispatch. */
                vector etu, etv;
                tangent_basis(authored_n, &etu, &etv);
                vector offset = vector_sub(hp, scene->discs[i].center);
                float r = scene->discs[i].radius > 0.0f ? scene->discs[i].radius : 1.0f;
                h.entry_u = vector_dot(offset, etu) / r;
                h.entry_v = vector_dot(offset, etv) / r;
            }
            h.hit = 1;
            h.distance = t;
            h.object_id = RT_OBJ_ID(RT_OBJ_KIND_DISC, i);
        }
    }

    for (int i = 0; i < scene->cylinder_count; i++) {
        float t = rt_intersect_cylinder(ro, rd, &scene->cylinders[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_cylinder(hp, &scene->cylinders[i]);
            /* Two-sided so inside-view (camera enclosed by the cylinder)
             * shades and reflects against the inner wall — matches the
             * disc convention above. Without this, racer-style tunnels
             * built from cylinders go pitch-black inside. */
            if (vector_dot(h.normal, rd) > 0.0f)
                h.normal = vector_scale(h.normal, -1.0f);
            float u, v;
            uv_cylinder(hp, &scene->cylinders[i], &u, &v);
            const scene_material *m = &scene->materials[scene->cylinders[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.unlit = m->unlit;
            h.portal_index = m->portal_index;
            h.hit = 1;
            h.distance = t;
            h.object_id = RT_OBJ_ID(RT_OBJ_KIND_CYLINDER, i);
        }
    }

    for (int i = 0; i < scene->cone_count; i++) {
        float t = rt_intersect_cone(ro, rd, &scene->cones[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_cone(hp, &scene->cones[i]);
            float u, v;
            uv_cone(hp, &scene->cones[i], &u, &v);
            const scene_material *m = &scene->materials[scene->cones[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.unlit = m->unlit;
            h.portal_index = m->portal_index;
            h.hit = 1;
            h.distance = t;
            h.object_id = RT_OBJ_ID(RT_OBJ_KIND_CONE, i);
        }
    }

    for (int i = 0; i < scene->torus_count; i++) {
        float t = rt_intersect_torus(ro, rd, &scene->toruses[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_torus(hp, &scene->toruses[i]);
            float u, v;
            uv_torus(hp, &scene->toruses[i], &u, &v);
            const scene_material *m = &scene->materials[scene->toruses[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.unlit = m->unlit;
            h.portal_index = m->portal_index;
            h.hit = 1;
            h.distance = t;
            h.object_id = RT_OBJ_ID(RT_OBJ_KIND_TORUS, i);
        }
    }

    for (int i = 0; i < scene->triangle_count; i++) {
        float t = rt_intersect_triangle(ro, rd, &scene->triangles[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_triangle(&scene->triangles[i]);
            /* Scene triangles are double-sided: flip the geometric normal
             * to face the camera so the back side shades and reflects. */
            if (vector_dot(h.normal, rd) > 0.0f)
                h.normal = vector_scale(h.normal, -1.0f);
            float u, v;
            uv_planar(hp, scene->triangles[i].v0, h.normal, &u, &v);
            const scene_material *m = &scene->materials[scene->triangles[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.unlit = m->unlit;
            h.portal_index = m->portal_index;
            h.hit = 1;
            h.distance = t;
            h.object_id = RT_OBJ_ID(RT_OBJ_KIND_TRIANGLE, i);
        }
    }

    for (int i = 0; i < scene->mesh_count; i++) {
        const scene_mesh *mesh = &scene->meshes[i];
        const mat4 *winv = mesh_world_inv ? &mesh_world_inv[i] : NULL;

        /* Helper macro: writes the hit to `h` from a mesh intersection
         * (rt_mesh_hit `mh`, world hit point `hp`). Identical shape to
         * the standalone hit path, just inlined for both the original
         * and each virtual copy. */
        #define MESH_FILL_HIT(mhv, hpv) do {                              \
            h.point  = (hpv);                                             \
            h.normal = (mhv).normal;                                      \
            int _mat_idx = mesh->material_index;                          \
            if (_mat_idx < 0 || _mat_idx >= scene->material_count) {      \
                h.albedo = (scene_color){200, 200, 200};                  \
                h.reflectivity = 0.0f;                                    \
                h.unlit = 0;                                              \
                h.portal_index = -1;                                      \
            } else {                                                      \
                const scene_material *_m = &scene->materials[_mat_idx];   \
                h.albedo = material_sample(_m, scene->textures,           \
                                            (hpv), (mhv).u, (mhv).v);     \
                h.reflectivity = _m->reflectivity;                        \
                h.unlit = _m->unlit;                                      \
                h.portal_index = _m->portal_index;                        \
            }                                                             \
            h.hit = 1;                                                    \
            h.distance = (mhv).t;                                         \
            h.object_id = RT_OBJ_ID(RT_OBJ_KIND_MESH, i);                 \
        } while (0)

        /* --- Original mesh, clipped to all tagged portal half-spaces --- */
        rt_mesh_hit mh;
        if (rt_intersect_mesh(ro, rd, mesh, winv, &mh)) {
            if (mh.t > 0.0f && mh.t < closest_t) {
                vector hp = vector_add(ro, vector_scale(rd, mh.t));
                int keep = 1;
                for (int p = 0;
                     p < SCENE_MAX_PORTAL_TRAVERSALS && keep; p++) {
                    int trav = mesh->portal_disc1[p] - 1;
                    if (trav < 0 || trav >= scene->disc_count) continue;
                    const scene_disc *d = &scene->discs[trav];
                    if (vector_dot(vector_sub(hp, d->center),
                                    d->normal) < 0.0f)
                        keep = 0;
                }
                if (keep) {
                    closest_t = mh.t;
                    MESH_FILL_HIT(mh, hp);
                }
            }
        }

        /* --- One virtual copy per tagged portal --- */
        for (int p = 0; p < SCENE_MAX_PORTAL_TRAVERSALS; p++) {
            int trav = mesh->portal_disc1[p] - 1;
            if (trav < 0 || trav >= scene->disc_count) continue;
            const scene_disc *entry_disc = &scene->discs[trav];
            const scene_disc *exit_disc =
                partner_disc_for_entry(scene, entry_disc);
            if (!exit_disc) continue;

            /* P_BA = forward(exit, entry): inverse portal map by symmetry.
             * Compose with any existing mesh world-inv so a node-driven
             * mesh's pose is still honored. */
            mat4 P_BA = build_portal_forward_map(exit_disc, entry_disc);
            mat4 virt_winv = winv ? mat4_mul(*winv, P_BA) : P_BA;

            rt_mesh_hit mhv;
            if (rt_intersect_mesh(ro, rd, mesh, &virt_winv, &mhv)) {
                if (mhv.t > 0.0f && mhv.t < closest_t) {
                    vector hpv = vector_add(ro, vector_scale(rd, mhv.t));
                    float depth = vector_dot(
                        vector_sub(hpv, exit_disc->center),
                        exit_disc->normal);
                    if (depth >= 0.0f) {
                        closest_t = mhv.t;
                        MESH_FILL_HIT(mhv, hpv);
                    }
                }
            }
        }

        #undef MESH_FILL_HIT
    }

    for (int i = 0; i < scene->box_count; i++) {
        float t = rt_intersect_box(ro, rd, &scene->boxes[i]);
        if (t > 0.0f && t < closest_t) {
            closest_t = t;
            vector hp = vector_add(ro, vector_scale(rd, t));
            h.point = hp;
            h.normal = rt_normal_box(hp, &scene->boxes[i]);
            float u, v;
            rt_box_uv(hp, &scene->boxes[i], &u, &v);
            const scene_material *m = &scene->materials[scene->boxes[i].material];
            h.albedo = material_sample(m, scene->textures, hp, u, v);
            h.reflectivity = m->reflectivity;
            h.unlit = m->unlit;
            h.portal_index = m->portal_index;
            h.hit = 1;
            h.distance = t;
            h.object_id = RT_OBJ_ID(RT_OBJ_KIND_BOX, i);
        }
    }

    for (int i = 0; i < scene->sprite_count; i++) {
        vector spr_right, spr_up, spr_normal;
        float t = rt_intersect_sprite(ro, rd, &scene->sprites[i],
                                       camera_origin, &spr_right, &spr_up, &spr_normal);
        if (t > 0.0f && t < closest_t) {
            int frame_idx = rt_sprite_select_frame(&scene->sprites[i], camera_origin);
            const scene_frame *frame = &scene->sprites[i].frames[frame_idx];
            vector hp = vector_add(ro, vector_scale(rd, t));
            uint32_t pixel = rt_sprite_sample(&scene->sprites[i], frame,
                                               hp, spr_right, spr_up);
            uint8_t alpha = (pixel >> 24) & 0xFF;
            if (alpha == 0) continue;

            closest_t = t;
            h.point = hp;
            h.normal = spr_normal;
            h.albedo.r = (pixel >> 16) & 0xFF;
            h.albedo.g = (pixel >>  8) & 0xFF;
            h.albedo.b =  pixel        & 0xFF;
            h.reflectivity = 0.0f;
            h.unlit = 0;
            h.portal_index = -1;
            h.hit = 1;
            h.distance = t;
            h.object_id = RT_OBJ_ID(RT_OBJ_KIND_SPRITE, i);
        }
    }

    for (int i = 0; i < scene->heightfield_count; i++) {
        const scene_heightfield *hf = &scene->heightfields[i];
        float t;
        vector hn;
        int cell_r, cell_c;
        if (rt_intersect_heightfield(hf, ro, rd, &t, &hn, &cell_r, &cell_c)) {
            if (t > 0.0f && t < closest_t) {
                closest_t = t;
                h.point = vector_add(ro, vector_scale(rd, t));
                h.normal = hn;
                int cells_per_row = hf->cols - 1;
                int ci = (cell_r * cells_per_row + cell_c) * 3;
                scene_color cell = { hf->colors[ci], hf->colors[ci+1], hf->colors[ci+2] };
                if (hf->material >= 0 && hf->material < scene->material_count) {
                    /* Heightfield-specific shading: the material samples at
                       the hit point and modulates the per-cell biome color,
                       so procedural textures add sub-cell detail without
                       erasing the slope-aware palette baked into colors[]. */
                    const scene_material *m = &scene->materials[hf->material];
                    scene_color tex = material_sample(m, scene->textures,
                                                   h.point, h.point.x, h.point.z);
                    h.albedo.r = (uint8_t)(((int)cell.r * (int)tex.r) / 255);
                    h.albedo.g = (uint8_t)(((int)cell.g * (int)tex.g) / 255);
                    h.albedo.b = (uint8_t)(((int)cell.b * (int)tex.b) / 255);
                    h.reflectivity = m->reflectivity;
                    h.unlit = m->unlit;
                    h.portal_index = m->portal_index;
                } else {
                    h.albedo = cell;
                    h.reflectivity = 0.0f;
                    h.unlit = 0;
                    h.portal_index = -1;
                }
                h.hit = 1;
                h.distance = t;
                h.object_id = RT_OBJ_ID(RT_OBJ_KIND_HEIGHTFIELD, i);
            }
        }
    }

    return h;
}

/* Portal transform — pure math, shape-agnostic.
 *
 * Given a fully-resolved exit frame (`exit_point` + `exit_normal`) and the
 * entry's AUTHORED normal (NOT the camera-facing flipped one — that would
 * lose the "which side did we hit" information), produce the outgoing ray
 * by re-expressing the incoming direction in the exit frame with the
 * normal-axis component flipped ("going in becomes going out"). Because
 * we use the authored normal, the sign of dz encodes which side we hit:
 * front-hits emerge from the front, back-hits emerge from the back.
 *
 * Caller responsibilities (split out of this function so it stays pure):
 *   - Compute exit_point appropriately for the portal kind:
 *       FIXED      — stored target_position.
 *       RIGID disc — partner.center + world-space (u,v) offset reproduced.
 *       RIGID sph  — antipodal point on partner.
 *       PARAMETRIC — partner's inverse-UV at the entry's (entry_u, entry_v).
 *   - Compute exit_normal similarly (constant for discs, radial for spheres).
 *
 * The self-hit epsilon goes in the direction we're emerging, so front-exits
 * clear the exit's front face and back-exits clear its back. */
static void portal_transform(vector exit_point, vector exit_normal,
                             vector entry_normal, vector in_dir,
                             vector *out_origin, vector *out_dir) {
    vector etu, etv;
    tangent_basis(entry_normal, &etu, &etv);

    float dx = vector_dot(in_dir, etu);
    float dy = vector_dot(in_dir, etv);
    float dz = vector_dot(in_dir, entry_normal);

    vector xn = vector_normalize(exit_normal);
    vector xtu, xtv;
    tangent_basis(xn, &xtu, &xtv);

    /* Flip the normal axis: "going in" becomes "going out". For front hits
     * dz < 0, so the exit dz becomes positive (out of front); back hits
     * have dz > 0, exit dz is negative (out of back). */
    float exit_dz = -dz;
    vector nd = vector_add(vector_add(vector_scale(xtu, dx),
                                       vector_scale(xtv, dy)),
                            vector_scale(xn, exit_dz));
    nd = vector_normalize(nd);

    /* Step slightly along the exit normal in the direction we're going
     * (sign of exit_dz) so we clear the exit surface from the correct
     * side regardless of which side of the entry we hit. */
    float side = exit_dz >= 0.0f ? 1.0f : -1.0f;
    vector no = vector_add(exit_point,
                           vector_scale(xn, RT_REFLECT_EPSILON * side));

    *out_origin = no;
    *out_dir    = nd;
}

void rt_render_chunk(uint32_t *pixel_buf, rt_gbuffer *gbuf,
                     const rt_viewport *viewport,
                     int y_start, int y_end,
                     const scene_camera *camera, const scene *scene,
                     const mat4 *mesh_world_inv,
                     int interlace_field) {
    int width = viewport->width;
    int height = viewport->height;
    float fov_factor = (float)height / (2.0f * tanf(viewport->fov / 2.0f));

    float half_w = (float)width * 0.5f;
    float half_h = (float)height * 0.5f;

    int interlace = (interlace_field == 0 || interlace_field == 1);

    for (int y = y_start; y < y_end; y++) {
        if (interlace && (y & 1) != interlace_field) continue;
        for (int x = 0; x < width; x++) {
            float sx = ((float)x - half_w) / fov_factor;
            float sy = -((float)y - half_h) / fov_factor;

            vector dir = vector_add(
                vector_add(
                    camera->forward,
                    vector_scale(camera->right, sx)),
                vector_scale(camera->up, sy));
            dir = vector_normalize(dir);

            float result_r = 0.0f, result_g = 0.0f, result_b = 0.0f;
            float thr_r = 1.0f, thr_g = 1.0f, thr_b = 1.0f;
            vector ro = camera->origin;
            vector rd = dir;

            /* G-buffer capture follows reflection bounces: when the eye
             * looks at a mostly-mirror surface, the colour we composite
             * is dominated by the reflected scene, so the outline pass
             * needs to see the reflected geometry too. We accumulate
             * path length across mirror bounces and capture at the
             * first hit whose reflectivity is below the mirror
             * threshold (or on a sky miss). */
            const float RT_GBUF_MIRROR_THRESHOLD = 0.5f;
            int   gbuf_done = 0;
            float gbuf_depth_acc = 0.0f;

            for (int bounce = 0; bounce < RT_MAX_BOUNCES; bounce++) {
                hit_info h = closest_hit(ro, rd, scene, mesh_world_inv,
                                         camera->origin);

                /* Portal branch: when the hit is on a portal surface, skip
                 * shading and G-buffer capture (the portal is "see-through")
                 * and continue tracing from the exit frame. Depth still
                 * accumulates so a downstream G-buffer reflects the total
                 * path length, not just the trip to the portal. The
                 * RT_MAX_BOUNCES budget bounds infinite-portal chains. */
                if (h.hit && h.portal_index >= 0 &&
                    h.portal_index < scene->portal_count) {
                    const scene_portal *p = &scene->portals[h.portal_index];
                    vector exit_pos, exit_nrm;
                    int portal_ok = 0;
                    switch (p->kind) {
                        case SCENE_PORTAL_FIXED:
                            exit_pos = p->target_position;
                            exit_nrm = p->target_normal;
                            portal_ok = 1;
                            break;
                        case SCENE_PORTAL_PAIRED_RIGID:
                            switch (p->partner_kind) {
                                case SCENE_PRIM_DISC:
                                    /* Same world-space (u, v) offset
                                     * reproduced on the partner disc — a
                                     * literal geometric pass-through that
                                     * works cleanly when both discs share
                                     * scale. */
                                    if (p->partner_index >= 0 &&
                                        p->partner_index < scene->disc_count) {
                                        const scene_disc *pd =
                                            &scene->discs[p->partner_index];
                                        vector etu, etv;
                                        tangent_basis(h.entry_normal, &etu, &etv);
                                        vector off = vector_sub(h.point, h.entry_center);
                                        float ou = vector_dot(off, etu);
                                        float ov = vector_dot(off, etv);
                                        vector xtu, xtv;
                                        tangent_basis(pd->normal, &xtu, &xtv);
                                        exit_pos = vector_add(pd->center,
                                            vector_add(vector_scale(xtu, ou),
                                                       vector_scale(xtv, ov)));
                                        exit_nrm = pd->normal;
                                        portal_ok = 1;
                                    }
                                    break;
                                case SCENE_PRIM_SPHERE:
                                    /* Antipodal correspondence: hit at
                                     * angular direction dA on A → exit at
                                     * -dA on B's surface, with the exit
                                     * normal pointing outward (-dA) at that
                                     * point. Combined with portal_transform's
                                     * flip_z, this gives "exit direction =
                                     * in direction" — the Portal-tube look:
                                     * walk in any side of A, emerge from the
                                     * opposite side of B going the same way. */
                                    if (p->partner_index >= 0 &&
                                        p->partner_index < scene->sphere_count) {
                                        const scene_sphere *ps =
                                            &scene->spheres[p->partner_index];
                                        vector dA = h.entry_normal;
                                        exit_pos = vector_sub(ps->center,
                                            vector_scale(dA, ps->radius));
                                        exit_nrm = vector_scale(dA, -1.0f);
                                        portal_ok = 1;
                                    }
                                    break;
                            }
                            break;
                        case SCENE_PORTAL_PAIRED_PARAMETRIC: {
                            /* Same normalized (u, v) on the partner. The
                             * entry's (entry_u, entry_v) was already
                             * computed in the primitive's hit code; here we
                             * invert it on the partner's shape to get the
                             * exit point + normal. Works across mismatched
                             * shapes: disc → sphere wraps the disc's circle
                             * across the sphere's lat/long, sphere → disc
                             * flattens the sphere's surface onto the disc. */
                            switch (p->partner_kind) {
                                case SCENE_PRIM_DISC:
                                    if (p->partner_index >= 0 &&
                                        p->partner_index < scene->disc_count) {
                                        const scene_disc *pd =
                                            &scene->discs[p->partner_index];
                                        vector xtu, xtv;
                                        tangent_basis(pd->normal, &xtu, &xtv);
                                        exit_pos = vector_add(pd->center,
                                            vector_add(vector_scale(xtu, h.entry_u * pd->radius),
                                                       vector_scale(xtv, h.entry_v * pd->radius)));
                                        exit_nrm = pd->normal;
                                        portal_ok = 1;
                                    }
                                    break;
                                case SCENE_PRIM_SPHERE:
                                    if (p->partner_index >= 0 &&
                                        p->partner_index < scene->sphere_count) {
                                        const scene_sphere *ps =
                                            &scene->spheres[p->partner_index];
                                        float phi   = h.entry_u * (float)M_PI;
                                        float theta = (h.entry_v + 1.0f) * (float)M_PI * 0.5f;
                                        float st = sinf(theta), ct = cosf(theta);
                                        float cp = cosf(phi),   sp = sinf(phi);
                                        vector dB = { st * cp, ct, st * sp };
                                        exit_pos = vector_add(ps->center,
                                            vector_scale(dB, ps->radius));
                                        exit_nrm = dB;
                                        portal_ok = 1;
                                    }
                                    break;
                            }
                            break;
                        }
                    }
                    if (portal_ok) {
                        if (gbuf && !gbuf_done) gbuf_depth_acc += h.distance;
                        vector new_ro, new_rd;
                        portal_transform(exit_pos, exit_nrm,
                                         h.entry_normal, rd,
                                         &new_ro, &new_rd);
                        ro = new_ro;
                        rd = new_rd;
                        continue;
                    }
                    /* Mis-authored portal (e.g. partner index out of range):
                     * fall through and render the surface normally — better
                     * than crashing or showing a black hole. */
                }

                if (gbuf && !gbuf_done) {
                    if (h.hit) gbuf_depth_acc += h.distance;
                    int capture = !h.hit ||
                                  h.reflectivity <= RT_GBUF_MIRROR_THRESHOLD;
                    if (capture) {
                        int idx = y * width + x;
                        if (h.hit) {
                            gbuf->object_id[idx]   = h.object_id;
                            gbuf->depth[idx]       = gbuf_depth_acc;
                            gbuf->normal[idx*3+0]  = h.normal.x;
                            gbuf->normal[idx*3+1]  = h.normal.y;
                            gbuf->normal[idx*3+2]  = h.normal.z;
                        } else {
                            gbuf->object_id[idx]   = 0;
                            gbuf->depth[idx]       = FLT_MAX;
                            gbuf->normal[idx*3+0]  = 0.0f;
                            gbuf->normal[idx*3+1]  = 0.0f;
                            gbuf->normal[idx*3+2]  = 0.0f;
                        }
                        gbuf_done = 1;
                    }
                }

                if (!h.hit) break;

                float shade;
                if (h.unlit) {
                    shade = 1.0f;
                } else {
                    shade = scene->ambient;
                    for (int i = 0; i < scene->light_count; i++) {
                        float d = vector_dot(h.normal, scene->lights[i].direction);
                        if (d > 0.0f) shade += d * scene->lights[i].intensity;
                    }
                    if (shade > 1.0f) shade = 1.0f;
                }

                float dw = 1.0f - h.reflectivity;
                result_r += thr_r * dw * (float)h.albedo.r * shade;
                result_g += thr_g * dw * (float)h.albedo.g * shade;
                result_b += thr_b * dw * (float)h.albedo.b * shade;

                if (h.reflectivity <= 0.0f) break;

                thr_r *= h.reflectivity;
                thr_g *= h.reflectivity;
                thr_b *= h.reflectivity;

                float ndotrd = vector_dot(h.normal, rd);
                rd = vector_normalize(vector_sub(rd, vector_scale(h.normal, 2.0f * ndotrd)));
                /* Offset along the surface normal, not the reflected
                 * direction: at grazing reflection angles `rd` is nearly
                 * tangent to the surface, so a tiny step along it leaves
                 * the new origin within float epsilon of the same
                 * primitive and produces self-hit acne (black speckles
                 * on spherical mirrors). The normal is always
                 * perpendicular, so the offset clears the surface
                 * regardless of bounce angle. */
                ro = vector_add(h.point, vector_scale(h.normal, RT_REFLECT_EPSILON));
            }

            /* Edge case: the entire bounce budget was spent inside
             * mirror chains and we never landed on a non-reflective
             * surface. Treat as sky for the G-buffer so the edge filter
             * has consistent miss values. */
            if (gbuf && !gbuf_done) {
                int idx = y * width + x;
                gbuf->object_id[idx]   = 0;
                gbuf->depth[idx]       = FLT_MAX;
                gbuf->normal[idx*3+0]  = 0.0f;
                gbuf->normal[idx*3+1]  = 0.0f;
                gbuf->normal[idx*3+2]  = 0.0f;
            }

            uint8_t cr = result_r > 255.0f ? 255 : (result_r < 0.0f ? 0 : (uint8_t)result_r);
            uint8_t cg = result_g > 255.0f ? 255 : (result_g < 0.0f ? 0 : (uint8_t)result_g);
            uint8_t cb = result_b > 255.0f ? 255 : (result_b < 0.0f ? 0 : (uint8_t)result_b);

            pixel_buf[y * width + x] = (255u << 24) | (cr << 16) | (cg << 8) | cb;
        }
    }
}
