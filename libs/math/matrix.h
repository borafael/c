#ifndef MATH_MATRIX_H
#define MATH_MATRIX_H

#include <math.h>
#include "vector.h"

/* 4x4 affine transform, row-major: m[row * 4 + col].
 *
 * Conventions: column vectors right-multiplied by the matrix
 * (`p' = M * p`). `mat4_trs(t, e, s)` builds T * R * S, with
 * R = Rx * Ry * Rz (Tait-Bryan XYZ — Z applied first, then Y, then X)
 * to match scene_anim_sample's Euler channels and ufbx's
 * UFBX_ROTATION_ORDER_XYZ. The bottom row is always (0,0,0,1); the
 * helpers here assume that and skip projective math. */
typedef struct {
    float m[16];
} mat4;

static inline mat4 mat4_identity(void) {
    mat4 r = {{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    }};
    return r;
}

static inline mat4 mat4_mul(mat4 a, mat4 b) {
    mat4 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r.m[i * 4 + j] =
                a.m[i * 4 + 0] * b.m[0 * 4 + j] +
                a.m[i * 4 + 1] * b.m[1 * 4 + j] +
                a.m[i * 4 + 2] * b.m[2 * 4 + j] +
                a.m[i * 4 + 3] * b.m[3 * 4 + j];
        }
    }
    return r;
}

static inline vector mat4_transform_point(mat4 m, vector p) {
    return (vector){
        m.m[0]*p.x + m.m[1]*p.y + m.m[ 2]*p.z + m.m[ 3],
        m.m[4]*p.x + m.m[5]*p.y + m.m[ 6]*p.z + m.m[ 7],
        m.m[8]*p.x + m.m[9]*p.y + m.m[10]*p.z + m.m[11],
    };
}

static inline vector mat4_transform_dir(mat4 m, vector d) {
    return (vector){
        m.m[0]*d.x + m.m[1]*d.y + m.m[ 2]*d.z,
        m.m[4]*d.x + m.m[5]*d.y + m.m[ 6]*d.z,
        m.m[8]*d.x + m.m[9]*d.y + m.m[10]*d.z,
    };
}

static inline mat4 mat4_translate(vector t) {
    mat4 r = mat4_identity();
    r.m[3]  = t.x;
    r.m[7]  = t.y;
    r.m[11] = t.z;
    return r;
}

static inline mat4 mat4_scale(vector s) {
    mat4 r = mat4_identity();
    r.m[0]  = s.x;
    r.m[5]  = s.y;
    r.m[10] = s.z;
    return r;
}

/* R = Rx * Ry * Rz (apply Z first, then Y, then X). Matches the order in
 * scene_anim_sample / ufbx XYZ / FBX. */
static inline mat4 mat4_rotate_xyz(vector e) {
    float sx = sinf(e.x), cx = cosf(e.x);
    float sy = sinf(e.y), cy = cosf(e.y);
    float sz = sinf(e.z), cz = cosf(e.z);
    mat4 r = mat4_identity();
    /* Closed-form expansion of Rx * Ry * Rz. */
    r.m[ 0] =  cy * cz;
    r.m[ 1] = -cy * sz;
    r.m[ 2] =  sy;
    r.m[ 4] =  cx * sz + sx * sy * cz;
    r.m[ 5] =  cx * cz - sx * sy * sz;
    r.m[ 6] = -sx * cy;
    r.m[ 8] =  sx * sz - cx * sy * cz;
    r.m[ 9] =  sx * cz + cx * sy * sz;
    r.m[10] =  cx * cy;
    return r;
}

static inline mat4 mat4_trs(vector t, vector e, vector s) {
    /* T * R * S — directly composed without three muls so we don't pay
     * the round-trip cost. The 3x3 upper-left is R * S; the last column
     * is t. */
    mat4 R = mat4_rotate_xyz(e);
    mat4 r = R;
    r.m[ 0] *= s.x; r.m[ 1] *= s.y; r.m[ 2] *= s.z;
    r.m[ 4] *= s.x; r.m[ 5] *= s.y; r.m[ 6] *= s.z;
    r.m[ 8] *= s.x; r.m[ 9] *= s.y; r.m[10] *= s.z;
    r.m[ 3] = t.x;
    r.m[ 7] = t.y;
    r.m[11] = t.z;
    return r;
}

/* Inverse of an affine matrix (last row = 0,0,0,1). For a TRS with
 * non-zero scale this is exact; for a general affine it inverts the
 * upper 3x3 by cofactor expansion. */
static inline mat4 mat4_affine_inverse(mat4 m) {
    float a = m.m[0], b = m.m[1], c = m.m[ 2];
    float d = m.m[4], e = m.m[5], f = m.m[ 6];
    float g = m.m[8], h = m.m[9], i = m.m[10];
    float det = a * (e * i - f * h)
              - b * (d * i - f * g)
              + c * (d * h - e * g);
    mat4 r = mat4_identity();
    if (fabsf(det) < 1e-12f) return r;  /* singular — best-effort identity */
    float inv_det = 1.0f / det;
    /* Inverse of the upper-left 3x3 (transpose of cofactor / det). */
    float inv00 =  (e * i - f * h) * inv_det;
    float inv01 = -(b * i - c * h) * inv_det;
    float inv02 =  (b * f - c * e) * inv_det;
    float inv10 = -(d * i - f * g) * inv_det;
    float inv11 =  (a * i - c * g) * inv_det;
    float inv12 = -(a * f - c * d) * inv_det;
    float inv20 =  (d * h - e * g) * inv_det;
    float inv21 = -(a * h - b * g) * inv_det;
    float inv22 =  (a * e - b * d) * inv_det;
    /* Translation: -A^-1 * t. */
    float tx = m.m[3], ty = m.m[7], tz = m.m[11];
    r.m[ 0] = inv00; r.m[ 1] = inv01; r.m[ 2] = inv02;
    r.m[ 4] = inv10; r.m[ 5] = inv11; r.m[ 6] = inv12;
    r.m[ 8] = inv20; r.m[ 9] = inv21; r.m[10] = inv22;
    r.m[ 3] = -(inv00 * tx + inv01 * ty + inv02 * tz);
    r.m[ 7] = -(inv10 * tx + inv11 * ty + inv12 * tz);
    r.m[11] = -(inv20 * tx + inv21 * ty + inv22 * tz);
    return r;
}

/* Transforms a normal: n_out = transpose(inverse(M_3x3)) * n. Caller
 * normalizes if needed. Uses `m_inv` (the affine inverse) so callers
 * can pass the matrix they already computed. */
static inline vector mat4_transform_normal(mat4 m_inv, vector n) {
    return (vector){
        m_inv.m[0]*n.x + m_inv.m[4]*n.y + m_inv.m[ 8]*n.z,
        m_inv.m[1]*n.x + m_inv.m[5]*n.y + m_inv.m[ 9]*n.z,
        m_inv.m[2]*n.x + m_inv.m[6]*n.y + m_inv.m[10]*n.z,
    };
}

#endif
