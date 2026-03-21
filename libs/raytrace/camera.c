#include "camera.h"
#include <stdlib.h>

static void camera_update_orientation(rt_camera *cam) {
    cam->forward = vector_normalize(cam->forward);

    vector world_up = {0.0f, 1.0f, 0.0f};
    cam->right = vector_normalize(vector_cross(cam->forward, world_up));
    /* Handle degenerate case when looking straight up/down */
    if (vector_magnitude(cam->right) < 0.001f) {
        cam->right = (vector){1.0f, 0.0f, 0.0f};
    }
    cam->up = vector_cross(cam->right, cam->forward);
}

rt_camera *rt_camera_create(vector position, vector direction) {
    rt_camera *cam = malloc(sizeof(rt_camera));
    if (!cam) return NULL;
    cam->origin = position;
    cam->forward = direction;
    camera_update_orientation(cam);
    return cam;
}

void rt_camera_place(rt_camera *cam, vector position, vector direction) {
    cam->origin = position;
    cam->forward = direction;
    camera_update_orientation(cam);
}

void rt_camera_destroy(rt_camera *cam) {
    if (!cam) return;
    free(cam);
}

void rt_camera_get_basis(const rt_camera *cam,
                         vector *origin, vector *forward,
                         vector *right, vector *up) {
    *origin  = cam->origin;
    *forward = cam->forward;
    *right   = cam->right;
    *up      = cam->up;
}
