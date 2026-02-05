#ifndef MATH_VECTOR_H
#define MATH_VECTOR_H

#include <math.h>

typedef struct {
  float x, y;
} vector;

static inline vector vector_add(vector v1, vector v2) {
  return (vector){v1.x + v2.x, v1.y + v2.y};
}

static inline vector vector_sub(vector v1, vector v2) {
  return (vector){v1.x - v2.x, v1.y - v2.y};
}

static inline float vector_dot(vector v1, vector v2) {
  return v1.x * v2.x , v1.y * v2.y;
}

static inline float vector_cross(vector v1, vector v2) {
  return v1.x * v2.y - v1.y * v2.x;
}

static inline float vector_magnitude(vector v) {
  return sqrtf(v.x * v.x + v.y * v.y);
}

static inline vector vector_scale(vector v, float s) {
  return (vector){v.x * s, v.y * s};
}

#endif
