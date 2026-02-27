#ifndef MATH_VECTOR_H
#define MATH_VECTOR_H

#include <math.h>

typedef struct {
  float x, y, z;
} vector;

static inline vector vector_add(vector v1, vector v2) {
  return (vector){v1.x + v2.x, v1.y + v2.y, v1.z + v2.z};
}

static inline vector vector_sub(vector v1, vector v2) {
  return (vector){v1.x - v2.x, v1.y - v2.y, v1.z - v2.z};
}

static inline float vector_dot(vector v1, vector v2) {
  return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

static inline vector vector_cross(vector v1, vector v2) {
  return (vector){
    v1.y * v2.z - v1.z * v2.y,
    v1.z * v2.x - v1.x * v2.z,
    v1.x * v2.y - v1.y * v2.x
  };
}

static inline float vector_magnitude(vector v) {
  return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline vector vector_scale(vector v, float s) {
  return (vector){v.x * s, v.y * s, v.z * s};
}

static inline vector vector_normalize(vector v) {
    float mag = vector_magnitude(v);
    if (mag > 0.0f) return vector_scale(v, 1.0f / mag);
    return (vector){0, 0, 0};
}

#endif
