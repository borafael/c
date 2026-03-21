#ifndef RT_CAMERA_H
#define RT_CAMERA_H

#include "vector.h"

typedef struct {
    vector origin;
    vector forward;
    vector right;
    vector up;
} rt_camera;

rt_camera *rt_camera_create(vector position, vector direction);
void rt_camera_place(rt_camera *cam, vector position, vector direction);
void rt_camera_destroy(rt_camera *cam);
void rt_camera_get_basis(const rt_camera *cam,
                         vector *origin, vector *forward,
                         vector *right, vector *up);

#endif /* RT_CAMERA_H */
