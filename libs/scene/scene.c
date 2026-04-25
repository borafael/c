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
    s->skin_capacity         = SCENE_DEFAULT_CAPACITY;
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
    s->skins        = malloc(sizeof(scene_skin)        * s->skin_capacity);
    s->nodes        = malloc(sizeof(scene_node)        * s->node_capacity);
    s->animations   = malloc(sizeof(scene_animation)   * s->animation_capacity);

    if (!s->spheres || !s->planes || !s->discs || !s->cylinders ||
        !s->triangles || !s->boxes || !s->sprites || !s->heightfields ||
        !s->lights || !s->materials || !s->textures ||
        !s->meshes || !s->skins || !s->nodes || !s->animations) {
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

static void free_owned_skin_buffers(scene *s) {
    for (int i = 0; i < s->skin_count; i++) {
        scene_skin *sk = &s->skins[i];
        free(sk->bones);
        free(sk->influences);
        free(sk->rest_positions);
        free(sk->rest_normals);
        sk->bones = NULL;
        sk->influences = NULL;
        sk->rest_positions = NULL;
        sk->rest_normals = NULL;
        sk->bone_count = 0;
        sk->vertex_count = 0;
    }
}

void scene_clear(scene *s) {
    if (!s) return;
    free_owned_mesh_buffers(s);
    free_owned_animation_buffers(s);
    free_owned_skin_buffers(s);
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
    s->skin_count        = 0;
    s->node_count        = 0;
    s->animation_count   = 0;
}

void scene_destroy(scene *s) {
    if (!s) return;
    free_owned_mesh_buffers(s);
    free_owned_animation_buffers(s);
    free_owned_skin_buffers(s);
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
    free(s->skins);
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
    /* Normalize skin_index so zero-initialized callers (and any out-of-range
     * value) default to "rigid mesh, no skin". Callers attaching a skin
     * must add the skin first so skin_count covers their index. */
    if (mesh.skin_index < 0 || mesh.skin_index >= s->skin_count) {
        mesh.skin_index = -1;
    }
    int idx = s->mesh_count;
    s->meshes[s->mesh_count++] = mesh;
    return idx;
}

int scene_add_skin(scene *s, scene_skin skin) {
    GROW_IF_NEEDED(s->skins, s->skin_count, s->skin_capacity, scene_skin);
    int idx = s->skin_count;
    s->skins[s->skin_count++] = skin;
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

/* ============================== Skinning ================================= */

/* Returns transform_dir applied with the upper-3x3 of `m`. Used for
 * normal blending where we have already computed the normal-space
 * matrix (transpose of inverse) — passing the matrix verbatim to
 * mat4_transform_normal already does that, but we want raw upper-3x3
 * here, so we pre-transpose externally. */
static inline vector mat3_apply(const mat4 *m, vector v) {
    return (vector){
        m->m[0]*v.x + m->m[1]*v.y + m->m[ 2]*v.z,
        m->m[4]*v.x + m->m[5]*v.y + m->m[ 6]*v.z,
        m->m[8]*v.x + m->m[9]*v.y + m->m[10]*v.z,
    };
}

void scene_apply_skinning(scene *s, const mat4 *node_world) {
    if (!s || !node_world || s->skin_count <= 0) return;

    /* Per-bone scratch — at most one mat4 per bone for the skin matrix
     * and one for its inverse-transpose-3x3 (used for normals). Bone
     * counts are small (typically <256), so a stack-or-malloc per skin
     * is fine. */
    for (int mi = 0; mi < s->mesh_count; mi++) {
        scene_mesh *mesh = &s->meshes[mi];
        if (mesh->skin_index < 0 || mesh->skin_index >= s->skin_count) continue;
        const scene_skin *sk = &s->skins[mesh->skin_index];
        if (sk->bone_count <= 0 || sk->vertex_count != mesh->vertex_count) continue;

        mat4 *skin_mat   = malloc(sizeof(mat4) * (size_t)sk->bone_count);
        mat4 *normal_mat = malloc(sizeof(mat4) * (size_t)sk->bone_count);
        if (!skin_mat || !normal_mat) {
            free(skin_mat); free(normal_mat);
            continue;
        }

        /* Pull vertices into the owning node's local frame so the
         * renderer's existing rigid path (apply owning_node world) places
         * them in world space exactly once, even if the owning node is
         * animated. */
        mat4 owning_inv = mat4_identity();
        if (sk->owning_node_index >= 0 && sk->owning_node_index < s->node_count) {
            owning_inv = mat4_affine_inverse(node_world[sk->owning_node_index]);
        }

        for (int b = 0; b < sk->bone_count; b++) {
            int ni = sk->bones[b].bone_node_index;
            mat4 bone_world = (ni >= 0 && ni < s->node_count)
                              ? node_world[ni]
                              : mat4_identity();
            skin_mat[b]   = mat4_mul(owning_inv,
                                     mat4_mul(bone_world, sk->bones[b].bind_inv));
            /* Normal matrix = inverse-transpose of upper 3x3. We pack it
             * back into a mat4 (translation cleared) so we can reuse
             * mat3_apply: stash transpose(inverse(M_3x3)) in the upper
             * 3x3, with column-major in the row slots so mat3_apply does
             * the right multiply. The shortcut mat4_transform_normal
             * builds this on the fly from the affine inverse — we hoist
             * that work to once-per-bone. */
            mat4 inv = mat4_affine_inverse(skin_mat[b]);
            mat4 nm  = mat4_identity();
            nm.m[ 0] = inv.m[0]; nm.m[ 1] = inv.m[4]; nm.m[ 2] = inv.m[ 8];
            nm.m[ 4] = inv.m[1]; nm.m[ 5] = inv.m[5]; nm.m[ 6] = inv.m[ 9];
            nm.m[ 8] = inv.m[2]; nm.m[ 9] = inv.m[6]; nm.m[10] = inv.m[10];
            normal_mat[b] = nm;
        }

        for (int v = 0; v < sk->vertex_count; v++) {
            const scene_skin_vertex *iv = &sk->influences[v];
            vector pos_rest = sk->rest_positions[v];
            vector nrm_rest = sk->rest_normals[v];

            vector pos = {0, 0, 0};
            vector nrm = {0, 0, 0};
            float total_w = 0.0f;
            for (int k = 0; k < SCENE_SKIN_INFLUENCES_PER_VERTEX; k++) {
                int32_t bi = iv->bone[k];
                float   w  = iv->weight[k];
                if (bi < 0 || w == 0.0f) continue;
                if (bi >= sk->bone_count) continue;
                vector bp = mat4_transform_point(skin_mat[bi], pos_rest);
                vector bn = mat3_apply(&normal_mat[bi], nrm_rest);
                pos.x += w * bp.x; pos.y += w * bp.y; pos.z += w * bp.z;
                nrm.x += w * bn.x; nrm.y += w * bn.y; nrm.z += w * bn.z;
                total_w += w;
            }
            if (total_w == 0.0f) {
                /* Vertex with no influences: preserve rest pose. */
                pos = pos_rest;
                nrm = nrm_rest;
            }
            mesh->vertices[v].position = pos;
            mesh->vertices[v].normal   = vector_normalize(nrm);
        }

        scene_mesh_compute_bounds(mesh);

        free(skin_mat);
        free(normal_mat);
    }
}

void scene_resolve_world_transforms(const scene *s, mat4 *out_world) {
    if (!s || !out_world) return;
    for (int i = 0; i < s->node_count; i++) {
        const scene_node *n = &s->nodes[i];
        mat4 local = mat4_trs(n->transform.position,
                              n->transform.rotation,
                              n->transform.scale);
        if (n->parent_index < 0) {
            out_world[i] = local;
        } else {
            out_world[i] = mat4_mul(out_world[n->parent_index], local);
        }
    }
}

void scene_set_ambient(scene *s, float ambient) {
    s->ambient = ambient;
}
