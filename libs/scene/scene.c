#include "scene.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SCENE_DEFAULT_CAPACITY 64

#define GROW_IF_NEEDED(arr, count, cap, type) do {                         \
    if ((count) >= (cap)) {                                                \
        int _new_cap = (cap) * 2;                                          \
        type *_new_arr = realloc((arr), sizeof(type) * _new_cap);          \
        if (!_new_arr) return -1;                                          \
        (arr) = _new_arr;                                                  \
        (cap) = _new_cap;                                                  \
    }                                                                      \
} while (0)

/* ============================== Helpers ================================== */

scene_transform scene_transform_identity(void) {
    scene_transform t;
    t.position = (vector){0, 0, 0};
    t.rotation = (vector){0, 0, 0};
    t.scale    = (vector){1, 1, 1};
    return t;
}

scene_material scene_material_default(void) {
    scene_material m;
    m.albedo       = (scene_color){255, 255, 255};
    m.albedo2      = (scene_color){0, 0, 0};
    m.tex_kind     = SCENE_TEX_NONE;
    m.tex_scale    = 1.0f;
    m.tex_index    = 0;
    m.reflectivity = 0.0f;
    m.unlit        = 0;
    return m;
}

/* ============================== Camera =================================== */

static void camera_update_orientation(scene_camera *cam) {
    cam->forward = vector_normalize(cam->forward);

    vector world_up = {0.0f, 1.0f, 0.0f};
    /* right = world_up × forward so that with forward toward the scene,
       right points toward the camera's actual right. */
    cam->right = vector_normalize(vector_cross(world_up, cam->forward));
    if (vector_magnitude(cam->right) < 0.001f) {
        cam->right = (vector){1.0f, 0.0f, 0.0f};
    }
    cam->up = vector_cross(cam->forward, cam->right);
}

scene_camera *scene_camera_create(vector position, vector direction) {
    scene_camera *cam = malloc(sizeof(scene_camera));
    if (!cam) return NULL;
    cam->origin  = position;
    cam->forward = direction;
    camera_update_orientation(cam);
    return cam;
}

void scene_camera_place(scene_camera *cam, vector position, vector direction) {
    cam->origin  = position;
    cam->forward = direction;
    camera_update_orientation(cam);
}

void scene_camera_destroy(scene_camera *cam) {
    if (!cam) return;
    free(cam);
}

void scene_camera_get_basis(const scene_camera *cam,
                            vector *origin, vector *forward,
                            vector *right,  vector *up) {
    *origin  = cam->origin;
    *forward = cam->forward;
    *right   = cam->right;
    *up      = cam->up;
}

/* ============================== Scene ==================================== */

scene *scene_create(void) {
    scene *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->sphere_capacity       = SCENE_DEFAULT_CAPACITY;
    s->plane_capacity        = SCENE_DEFAULT_CAPACITY;
    s->disc_capacity         = SCENE_DEFAULT_CAPACITY;
    s->cylinder_capacity     = SCENE_DEFAULT_CAPACITY;
    s->triangle_capacity     = SCENE_DEFAULT_CAPACITY;
    s->box_capacity          = SCENE_DEFAULT_CAPACITY;
    s->sprite_capacity       = SCENE_DEFAULT_CAPACITY;
    s->heightfield_capacity  = SCENE_DEFAULT_CAPACITY;
    s->light_capacity        = SCENE_DEFAULT_CAPACITY;
    s->material_capacity     = SCENE_DEFAULT_CAPACITY;
    s->texture_capacity      = SCENE_DEFAULT_CAPACITY;
    s->mesh_capacity         = SCENE_DEFAULT_CAPACITY;
    s->node_capacity         = SCENE_DEFAULT_CAPACITY;
    s->animation_capacity    = SCENE_DEFAULT_CAPACITY;
    s->ambient               = 0.15f;

    s->spheres      = malloc(sizeof(scene_sphere)      * s->sphere_capacity);
    s->planes       = malloc(sizeof(scene_plane)       * s->plane_capacity);
    s->discs        = malloc(sizeof(scene_disc)        * s->disc_capacity);
    s->cylinders    = malloc(sizeof(scene_cylinder)    * s->cylinder_capacity);
    s->triangles    = malloc(sizeof(scene_triangle)    * s->triangle_capacity);
    s->boxes        = malloc(sizeof(scene_box)         * s->box_capacity);
    s->sprites      = malloc(sizeof(scene_sprite)      * s->sprite_capacity);
    s->heightfields = malloc(sizeof(scene_heightfield) * s->heightfield_capacity);
    s->lights       = malloc(sizeof(scene_light)       * s->light_capacity);
    s->materials    = malloc(sizeof(scene_material)    * s->material_capacity);
    s->textures     = malloc(sizeof(scene_texture)     * s->texture_capacity);
    s->meshes       = malloc(sizeof(scene_mesh)        * s->mesh_capacity);
    s->nodes        = malloc(sizeof(scene_node)        * s->node_capacity);
    s->animations   = malloc(sizeof(scene_animation)   * s->animation_capacity);

    if (!s->spheres || !s->planes || !s->discs || !s->cylinders ||
        !s->triangles || !s->boxes || !s->sprites || !s->heightfields ||
        !s->lights || !s->materials || !s->textures ||
        !s->meshes || !s->nodes || !s->animations) {
        scene_destroy(s);
        return NULL;
    }
    return s;
}

static void free_owned_mesh_buffers(scene *s) {
    for (int i = 0; i < s->mesh_count; i++) {
        free(s->meshes[i].vertices);
        free(s->meshes[i].indices);
        free(s->meshes[i].accel);
        s->meshes[i].accel = NULL;
        s->meshes[i].accel_count = 0;
    }
}

static void free_owned_animation_buffers(scene *s) {
    for (int i = 0; i < s->animation_count; i++) {
        scene_animation *a = &s->animations[i];
        for (int t = 0; t < a->track_count; t++) {
            free(a->tracks[t].keys);
        }
        free(a->tracks);
        a->tracks = NULL;
        a->track_count = 0;
    }
}

void scene_clear(scene *s) {
    if (!s) return;
    free_owned_mesh_buffers(s);
    free_owned_animation_buffers(s);
    s->sphere_count      = 0;
    s->plane_count       = 0;
    s->disc_count        = 0;
    s->cylinder_count    = 0;
    s->triangle_count    = 0;
    s->box_count         = 0;
    s->sprite_count      = 0;
    s->heightfield_count = 0;
    s->light_count       = 0;
    s->material_count    = 0;
    s->texture_count     = 0;
    s->mesh_count        = 0;
    s->node_count        = 0;
    s->animation_count   = 0;
}

void scene_destroy(scene *s) {
    if (!s) return;
    free_owned_mesh_buffers(s);
    free_owned_animation_buffers(s);
    free(s->spheres);
    free(s->planes);
    free(s->discs);
    free(s->cylinders);
    free(s->triangles);
    free(s->boxes);
    free(s->sprites);
    free(s->heightfields);
    free(s->lights);
    free(s->materials);
    free(s->textures);
    free(s->meshes);
    free(s->nodes);
    free(s->animations);
    free(s);
}

int scene_add_sphere(scene *s, scene_sphere sphere) {
    GROW_IF_NEEDED(s->spheres, s->sphere_count, s->sphere_capacity, scene_sphere);
    int idx = s->sphere_count;
    s->spheres[s->sphere_count++] = sphere;
    return idx;
}

int scene_add_plane(scene *s, scene_plane plane) {
    GROW_IF_NEEDED(s->planes, s->plane_count, s->plane_capacity, scene_plane);
    int idx = s->plane_count;
    s->planes[s->plane_count++] = plane;
    return idx;
}

int scene_add_disc(scene *s, scene_disc disc) {
    GROW_IF_NEEDED(s->discs, s->disc_count, s->disc_capacity, scene_disc);
    int idx = s->disc_count;
    s->discs[s->disc_count++] = disc;
    return idx;
}

int scene_add_cylinder(scene *s, scene_cylinder cylinder) {
    GROW_IF_NEEDED(s->cylinders, s->cylinder_count, s->cylinder_capacity, scene_cylinder);
    int idx = s->cylinder_count;
    s->cylinders[s->cylinder_count++] = cylinder;
    return idx;
}

int scene_add_triangle(scene *s, scene_triangle triangle) {
    GROW_IF_NEEDED(s->triangles, s->triangle_count, s->triangle_capacity, scene_triangle);
    int idx = s->triangle_count;
    s->triangles[s->triangle_count++] = triangle;
    return idx;
}

int scene_add_box(scene *s, scene_box box) {
    GROW_IF_NEEDED(s->boxes, s->box_count, s->box_capacity, scene_box);
    int idx = s->box_count;
    s->boxes[s->box_count++] = box;
    return idx;
}

int scene_add_sprite(scene *s, scene_sprite sprite) {
    GROW_IF_NEEDED(s->sprites, s->sprite_count, s->sprite_capacity, scene_sprite);
    int idx = s->sprite_count;
    s->sprites[s->sprite_count++] = sprite;
    return idx;
}

int scene_add_heightfield(scene *s, const scene_heightfield *hf) {
    GROW_IF_NEEDED(s->heightfields, s->heightfield_count, s->heightfield_capacity, scene_heightfield);
    int idx = s->heightfield_count;
    s->heightfields[s->heightfield_count++] = *hf;
    return idx;
}

int scene_add_light(scene *s, scene_light light) {
    GROW_IF_NEEDED(s->lights, s->light_count, s->light_capacity, scene_light);
    light.direction = vector_normalize(light.direction);
    int idx = s->light_count;
    s->lights[s->light_count++] = light;
    return idx;
}

int scene_add_material(scene *s, scene_material material) {
    GROW_IF_NEEDED(s->materials, s->material_count, s->material_capacity, scene_material);
    int idx = s->material_count;
    s->materials[s->material_count++] = material;
    return idx;
}

int scene_add_texture(scene *s, scene_texture texture) {
    GROW_IF_NEEDED(s->textures, s->texture_count, s->texture_capacity, scene_texture);
    int idx = s->texture_count;
    s->textures[s->texture_count++] = texture;
    return idx;
}

int scene_add_mesh(scene *s, scene_mesh mesh) {
    GROW_IF_NEEDED(s->meshes, s->mesh_count, s->mesh_capacity, scene_mesh);
    int idx = s->mesh_count;
    s->meshes[s->mesh_count++] = mesh;
    return idx;
}

void scene_mesh_compute_bounds(scene_mesh *mesh) {
    if (!mesh || mesh->vertex_count <= 0 || !mesh->vertices) {
        if (mesh) {
            mesh->bounds_center = (vector){0, 0, 0};
            mesh->bounds_radius = 0.0f;
        }
        return;
    }
    double cx = 0, cy = 0, cz = 0;
    for (int i = 0; i < mesh->vertex_count; i++) {
        cx += mesh->vertices[i].position.x;
        cy += mesh->vertices[i].position.y;
        cz += mesh->vertices[i].position.z;
    }
    float inv = 1.0f / (float)mesh->vertex_count;
    mesh->bounds_center = (vector){
        (float)cx * inv, (float)cy * inv, (float)cz * inv
    };
    float max_r2 = 0.0f;
    for (int i = 0; i < mesh->vertex_count; i++) {
        float dx = mesh->vertices[i].position.x - mesh->bounds_center.x;
        float dy = mesh->vertices[i].position.y - mesh->bounds_center.y;
        float dz = mesh->vertices[i].position.z - mesh->bounds_center.z;
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > max_r2) max_r2 = d2;
    }
    mesh->bounds_radius = sqrtf(max_r2);
}

int scene_add_node(scene *s, scene_node node) {
    GROW_IF_NEEDED(s->nodes, s->node_count, s->node_capacity, scene_node);
    int idx = s->node_count;
    s->nodes[s->node_count++] = node;
    return idx;
}

int scene_find_node_by_name(const scene *s, const char *name) {
    if (!s || !name || !*name) return -1;
    for (int i = 0; i < s->node_count; i++) {
        if (strcmp(s->nodes[i].name, name) == 0) return i;
    }
    return -1;
}

int scene_add_animation(scene *s, scene_animation anim) {
    GROW_IF_NEEDED(s->animations, s->animation_count, s->animation_capacity,
                   scene_animation);
    int idx = s->animation_count;
    s->animations[s->animation_count++] = anim;
    return idx;
}

/* Binary search for the keyframe interval containing `t`. Returns the
 * index of the left key; the right key is left+1. At the boundaries,
 * returns 0 or key_count-1 (caller clamps). */
static int anim_find_key(const scene_anim_key *keys, int count, float t) {
    if (count <= 1) return 0;
    if (t <= keys[0].time) return 0;
    if (t >= keys[count - 1].time) return count - 1;
    int lo = 0, hi = count - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) >> 1;
        if (keys[mid].time <= t) lo = mid;
        else                     hi = mid;
    }
    return lo;
}

static float anim_sample_track(const scene_anim_track *track, float t) {
    if (track->key_count == 0) return 0.0f;
    if (track->key_count == 1) return track->keys[0].value;
    int i = anim_find_key(track->keys, track->key_count, t);
    if (i >= track->key_count - 1) return track->keys[track->key_count - 1].value;
    const scene_anim_key *a = &track->keys[i];
    const scene_anim_key *b = &track->keys[i + 1];
    float dt = b->time - a->time;
    if (dt <= 0.0f) return a->value;
    float u = (t - a->time) / dt;
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    return a->value + (b->value - a->value) * u;
}

static float *channel_slot(scene_transform *tr, scene_anim_channel ch) {
    switch (ch) {
    case SCENE_ANIM_POS_X: return &tr->position.x;
    case SCENE_ANIM_POS_Y: return &tr->position.y;
    case SCENE_ANIM_POS_Z: return &tr->position.z;
    case SCENE_ANIM_ROT_X: return &tr->rotation.x;
    case SCENE_ANIM_ROT_Y: return &tr->rotation.y;
    case SCENE_ANIM_ROT_Z: return &tr->rotation.z;
    case SCENE_ANIM_SCL_X: return &tr->scale.x;
    case SCENE_ANIM_SCL_Y: return &tr->scale.y;
    case SCENE_ANIM_SCL_Z: return &tr->scale.z;
    default:               return NULL;
    }
}

void scene_anim_sample(scene *s, const scene_animation *anim,
                       float t, int loop) {
    if (!s || !anim || anim->track_count <= 0) return;
    float d = anim->duration;
    if (loop && d > 0.0f) {
        t = fmodf(t, d);
        if (t < 0.0f) t += d;
    } else {
        if (t < 0.0f)  t = 0.0f;
        if (t > d)     t = d;
    }
    for (int i = 0; i < anim->track_count; i++) {
        const scene_anim_track *tr = &anim->tracks[i];
        if (tr->node_index < 0 || tr->node_index >= s->node_count) continue;
        float *slot = channel_slot(&s->nodes[tr->node_index].transform,
                                   tr->channel);
        if (slot) *slot = anim_sample_track(tr, t);
    }
}

void scene_set_ambient(scene *s, float ambient) {
    s->ambient = ambient;
}
