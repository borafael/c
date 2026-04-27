/* Anim: end-to-end test of scene_animation + scene_anim_sample with the
 * full mesh-through-nodes pipeline.
 *
 * Default mode builds a three-segment "arm" (shoulder → elbow → wrist)
 * as nodes in the scene's node tree, each owning a small UV-sphere mesh.
 * The renderer resolves world transforms from the node hierarchy and
 * intersects each mesh in its own local space.
 *
 * Pass --load-fbx <path> to swap in any FBX file. The first animation
 * clip in the file (if present) plays on a loop. Skinned meshes load in
 * rest pose only — bone tracks still animate the node tree but don't
 * deform the mesh; see SCENE_FBX_ALLOW_SKINNED in libs/scene/fbx.h.
 *
 * Controls:
 *   Left-drag       orbit around target
 *   Right-drag      pan target in screen plane
 *   Scroll wheel    zoom (multiplicative on distance)
 *   R               reset camera to auto-framed view
 *   SPACE           pause/resume animation
 *   ESC             quit
 */

#include "renderer.h"
#include "viewport.h"
#include "mesh.h"
#include "scene.h"
#include "fbx.h"
#include "plane.h"

#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

#define WINDOW_W     960
#define WINDOW_H     600
#define FOV          (M_PI / 3.0f)
#define NUM_SEGMENTS 3

/* UV-sphere: rings = latitude bands, sectors = longitude slices. Small
 * counts are fine here — the demo wants a sphere-shaped mesh, not high
 * fidelity. Vertices and indices are heap-allocated and ownership is
 * transferred to the scene via scene_add_mesh. */
static scene_mesh make_uv_sphere(float radius, int material_index,
                                  int rings, int sectors) {
    int vcount = (rings + 1) * (sectors + 1);
    int icount = rings * sectors * 6;
    scene_vertex *verts = malloc(sizeof(*verts) * (size_t)vcount);
    uint32_t     *inds  = malloc(sizeof(*inds)  * (size_t)icount);

    int vi = 0;
    for (int r = 0; r <= rings; r++) {
        float v = (float)r / (float)rings;
        float phi = v * (float)M_PI;
        float sp = sinf(phi), cp = cosf(phi);
        for (int s = 0; s <= sectors; s++) {
            float u = (float)s / (float)sectors;
            float theta = u * 2.0f * (float)M_PI;
            float st = sinf(theta), ct = cosf(theta);
            vector n = { sp * ct, cp, sp * st };
            verts[vi].position = (vector){ radius * n.x, radius * n.y, radius * n.z };
            verts[vi].normal   = n;
            verts[vi].u = u;
            verts[vi].v = v;
            vi++;
        }
    }

    int ii = 0;
    int stride = sectors + 1;
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < sectors; s++) {
            uint32_t a = (uint32_t)( r      * stride + s);
            uint32_t b = (uint32_t)((r + 1) * stride + s);
            uint32_t c = (uint32_t)((r + 1) * stride + s + 1);
            uint32_t d = (uint32_t)( r      * stride + s + 1);
            inds[ii++] = a; inds[ii++] = b; inds[ii++] = c;
            inds[ii++] = a; inds[ii++] = c; inds[ii++] = d;
        }
    }

    scene_mesh m = {0};
    m.vertices = verts;
    m.vertex_count = vcount;
    m.indices = inds;
    m.index_count = ii;
    m.material_index = material_index;
    scene_mesh_compute_bounds(&m);
    return m;
}

static scene_anim_key *sine_track(int nframes, float duration,
                                  float amplitude, float cycles) {
    scene_anim_key *keys = malloc(sizeof(*keys) * nframes);
    for (int f = 0; f < nframes; f++) {
        float t = (float)f / (float)(nframes - 1) * duration;
        keys[f].time  = t;
        keys[f].value = amplitude *
                        sinf(t * cycles * 2.0f * (float)M_PI / duration);
    }
    return keys;
}

/* Accumulate one mesh-bearing node's world-space AABB contribution into
 * (mn, mx). Helper for the swept-animation framing pass below. */
static void accumulate_node_aabb(const scene *s, const mat4 *world,
                                 int node_idx,
                                 vector *mn, vector *mx, int *found) {
    int mi = s->nodes[node_idx].mesh_index;
    if (mi < 0 || mi >= s->mesh_count) return;
    const scene_mesh *m = &s->meshes[mi];
    if (m->bounds_radius <= 0.0f) return;

    vector c = mat4_transform_point(world[node_idx], m->bounds_center);
    float c0 = sqrtf(world[node_idx].m[0]*world[node_idx].m[0] + world[node_idx].m[4]*world[node_idx].m[4] + world[node_idx].m[ 8]*world[node_idx].m[ 8]);
    float c1 = sqrtf(world[node_idx].m[1]*world[node_idx].m[1] + world[node_idx].m[5]*world[node_idx].m[5] + world[node_idx].m[ 9]*world[node_idx].m[ 9]);
    float c2 = sqrtf(world[node_idx].m[2]*world[node_idx].m[2] + world[node_idx].m[6]*world[node_idx].m[6] + world[node_idx].m[10]*world[node_idx].m[10]);
    float smax = c0 > c1 ? c0 : c1; if (c2 > smax) smax = c2;
    float r = m->bounds_radius * smax;

    if (c.x - r < mn->x) mn->x = c.x - r;
    if (c.y - r < mn->y) mn->y = c.y - r;
    if (c.z - r < mn->z) mn->z = c.z - r;
    if (c.x + r > mx->x) mx->x = c.x + r;
    if (c.y + r > mx->y) mx->y = c.y + r;
    if (c.z + r > mx->z) mx->z = c.z + r;
    *found = 1;
}

/* Walks all mesh-bearing nodes, transforms each mesh's local bounds
 * sphere into world space, and accumulates an axis-aligned box around
 * the lot. Used to auto-frame the camera on an arbitrary loaded FBX. */
static int compute_world_aabb(const scene *s, vector *out_min, vector *out_max) {
    if (s->node_count <= 0 || s->mesh_count <= 0) return 0;
    mat4 *world = malloc(sizeof(*world) * (size_t)s->node_count);
    if (!world) return 0;
    scene_resolve_world_transforms(s, world);

    vector mn = { FLT_MAX,  FLT_MAX,  FLT_MAX};
    vector mx = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    int found = 0;
    for (int i = 0; i < s->node_count; i++) {
        accumulate_node_aabb(s, world, i, &mn, &mx, &found);
    }
    free(world);
    if (!found) return 0;
    *out_min = mn;
    *out_max = mx;
    return 1;
}

/* Sweeps the first animation clip across N evenly-spaced sample points
 * and returns the union of mesh world AABBs over the whole sweep. Lets
 * the orbit camera frame a moving rigid mesh so its full motion stays
 * in view. Mutates s->nodes during sampling, then re-samples at t=0 so
 * the first rendered frame is the clip's start pose. Falls back to a
 * single rest-pose AABB when the scene has no animations. */
static int compute_animated_world_aabb(scene *s, vector *out_min, vector *out_max) {
    if (s->animation_count == 0 || s->animations[0].duration <= 0.0f) {
        return compute_world_aabb(s, out_min, out_max);
    }
    const int samples = 16;
    mat4 *world = malloc(sizeof(*world) * (size_t)s->node_count);
    if (!world) return compute_world_aabb(s, out_min, out_max);

    vector mn = { FLT_MAX,  FLT_MAX,  FLT_MAX};
    vector mx = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    int found = 0;
    float duration = s->animations[0].duration;
    for (int k = 0; k < samples; k++) {
        float t = duration * (float)k / (float)(samples - 1);
        scene_anim_sample(s, &s->animations[0], t, 0);
        scene_resolve_world_transforms(s, world);
        for (int i = 0; i < s->node_count; i++) {
            accumulate_node_aabb(s, world, i, &mn, &mx, &found);
        }
    }
    free(world);
    /* Restore the start pose so the first rendered frame matches t=0. */
    scene_anim_sample(s, &s->animations[0], 0.0f, 0);

    if (!found) return 0;
    *out_min = mn;
    *out_max = mx;
    return 1;
}

/* Orbit camera state.
 *
 * Eye position is parameterized as `target + offset(yaw, pitch, distance)`,
 * where yaw=0 places the eye at +Z relative to target and increasing yaw
 * rotates the eye toward +X (around world Y). Pitch raises the eye above
 * the target; clamped just under ±90° so we never hit gimbal lock at the
 * poles. The "initial_*" fields are the auto-framed pose used by R-reset. */
typedef struct {
    vector target;
    float  yaw;
    float  pitch;
    float  distance;
    vector i_target;
    float  i_yaw;
    float  i_pitch;
    float  i_distance;
} orbit;

static void orbit_init_from_aabb(orbit *o, vector mn, vector mx) {
    vector center = { (mn.x + mx.x) * 0.5f,
                      (mn.y + mx.y) * 0.5f,
                      (mn.z + mx.z) * 0.5f };
    vector size   = { mx.x - mn.x, mx.y - mn.y, mx.z - mn.z };
    float diag = sqrtf(size.x*size.x + size.y*size.y + size.z*size.z);
    float d = diag * 1.4f;
    if (d < 1.0f) d = 1.0f;
    o->target   = center;
    o->yaw      = 0.7f;    /* ~40° around Y from +Z */
    o->pitch    = 0.3f;    /* ~17° above the equator */
    o->distance = d;
    o->i_target   = o->target;
    o->i_yaw      = o->yaw;
    o->i_pitch    = o->pitch;
    o->i_distance = o->distance;
}

static void orbit_apply(const orbit *o, scene_camera *cam) {
    float cp = cosf(o->pitch), sp = sinf(o->pitch);
    float cy = cosf(o->yaw),   sy = sinf(o->yaw);
    vector eye = { o->target.x + o->distance * sy * cp,
                   o->target.y + o->distance * sp,
                   o->target.z + o->distance * cy * cp };
    vector dir = vector_normalize(vector_sub(o->target, eye));
    scene_camera_place(cam, eye, dir);
}

static void orbit_drag_orbit(orbit *o, int dx, int dy) {
    o->yaw   -= (float)dx * 0.005f;
    o->pitch += (float)dy * 0.005f;
    const float lim = 1.55f;
    if (o->pitch >  lim) o->pitch =  lim;
    if (o->pitch < -lim) o->pitch = -lim;
}

static void orbit_drag_pan(orbit *o, const scene_camera *cam, int dx, int dy) {
    /* Move target so the scene appears to follow the cursor. Pan speed
     * scales with distance so close-up dragging stays precise. */
    float k = o->distance * 0.0015f;
    o->target = vector_sub(o->target, vector_scale(cam->right, (float)dx * k));
    o->target = vector_add(o->target, vector_scale(cam->up,    (float)dy * k));
}

static void orbit_zoom(orbit *o, int wheel) {
    float f = wheel > 0 ? powf(0.85f,  (float)wheel)
                        : powf(1.0f / 0.85f, (float)-wheel);
    o->distance *= f;
    if (o->distance < 0.01f) o->distance = 0.01f;
}

static void orbit_reset(orbit *o) {
    o->target   = o->i_target;
    o->yaw      = o->i_yaw;
    o->pitch    = o->i_pitch;
    o->distance = o->i_distance;
}

static int build_scene_from_fbx(const char *path, scene **scene_out) {
    scene *s = scene_create();
    if (!s) return 0;

    int first_node = 0;
    int n = scene_add_fbx(s, path, SCENE_FBX_DEFAULT, &first_node);
    if (n < 0) {
        fprintf(stderr, "anim: failed to load FBX: %s\n", path);
        scene_destroy(s);
        return 0;
    }
    fprintf(stderr,
            "anim: loaded %s — %d nodes, %d meshes, %d materials, %d clips\n",
            path, s->node_count, s->mesh_count,
            s->material_count, s->animation_count);
    for (int i = 0; i < s->animation_count; i++) {
        fprintf(stderr, "  clip %d: '%s' (%.2fs, %d tracks)\n",
                i, s->animations[i].name, s->animations[i].duration,
                s->animations[i].track_count);
    }

    /* Build CPU BVHs for every loaded mesh — required for the renderer's
     * mesh path. ufbx already gives us correct vertex normals. Ground
     * plane is added by main() after the animation sweep, so the floor
     * sits below the lowest point of the entire clip rather than the
     * rest pose. */
    rt_scene_build_accel(s);

    scene_set_ambient(s, 0.28f);
    scene_add_light(s, (scene_light){
        .direction = {0.35f, 0.90f, 0.45f}, .intensity = 0.9f
    });

    *scene_out = s;
    return 1;
}

static void build_scene(scene **scene_out) {
    scene *s = scene_create();

    /* Ground */
    int mat_ground = scene_add_material(s, (scene_material){
        .albedo    = {60, 95, 60},
        .albedo2   = {35, 60, 35},
        .tex_kind  = SCENE_TEX_CHECKER,
        .tex_scale = 1.0f,
    });
    scene_add_plane(s, (scene_plane){
        .normal = {0, 1, 0}, .point = {0, 0, 0}, .material = mat_ground
    });

    /* Three arm-segment colors / radii. */
    scene_color colors[NUM_SEGMENTS] = {
        {230,  80,  80},   /* shoulder – red */
        {230, 190,  70},   /* elbow    – amber */
        { 90, 190, 230},   /* wrist    – cyan */
    };
    float  radii[NUM_SEGMENTS]        = { 0.38f, 0.30f, 0.24f };
    vector local_offset[NUM_SEGMENTS] = {
        {0.0f, 4.0f, 0.0f},   /* shoulder — root at 4 units up */
        {0.0f, -1.5f, 0.0f},  /* elbow    — 1.5 down from shoulder */
        {0.0f, -1.5f, 0.0f},  /* wrist    — 1.5 down from elbow */
    };
    const char *names[NUM_SEGMENTS] = { "shoulder", "elbow", "wrist" };
    int parents[NUM_SEGMENTS]       = { -1, 0, 1 };
    int seg_node[NUM_SEGMENTS];

    for (int i = 0; i < NUM_SEGMENTS; i++) {
        int mat = scene_add_material(s, (scene_material){
            .albedo = colors[i], .reflectivity = 0.1f
        });
        /* One mesh per segment, vertices around the local origin so the
         * node's world transform is what places it. */
        int mesh_idx = scene_add_mesh(s, make_uv_sphere(radii[i], mat, 8, 12));
        rt_mesh_build_bvh(&s->meshes[mesh_idx]);

        scene_node n;
        memset(n.name, 0, sizeof(n.name));
        strncpy(n.name, names[i], sizeof(n.name) - 1);
        n.transform          = scene_transform_identity();
        n.transform.position = local_offset[i];
        n.parent_index       = parents[i];
        n.mesh_index         = mesh_idx;
        seg_node[i] = scene_add_node(s, n);
    }

    /* Synthetic 2-second clip: shoulder swings one full cycle, elbow
     * bends two cycles. Baked at 30 Hz to match SCENE_FBX_BAKE_HZ. */
    int   nframes  = 60;
    float duration = 2.0f;
    scene_anim_track *tracks = malloc(sizeof(*tracks) * 2);
    tracks[0] = (scene_anim_track){
        .node_index = seg_node[0], .channel = SCENE_ANIM_ROT_Z,
        .keys       = sine_track(nframes, duration, 0.8f, 1.0f),
        .key_count  = nframes,
    };
    tracks[1] = (scene_anim_track){
        .node_index = seg_node[1], .channel = SCENE_ANIM_ROT_Z,
        .keys       = sine_track(nframes, duration, 0.6f, 2.0f),
        .key_count  = nframes,
    };
    scene_animation anim;
    memset(&anim, 0, sizeof(anim));
    strcpy(anim.name, "swing");
    anim.duration    = duration;
    anim.tracks      = tracks;
    anim.track_count = 2;
    scene_add_animation(s, anim);

    scene_set_ambient(s, 0.28f);
    scene_add_light(s, (scene_light){
        .direction = {0.35f, 0.90f, 0.45f}, .intensity = 0.9f
    });

    *scene_out = s;
}

static void display_pixels(GLuint tex, GLuint fbo, const uint32_t *pixels,
                           int render_w, int render_h,
                           int window_w, int window_h) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, render_w, render_h,
                    GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, render_w, render_h,
                      0, window_h, window_w, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s [-G|--gpu] [--load-fbx <path>]\n"
            "  -G, --gpu          Start with the OpenGL backend (TAB toggles).\n"
            "  --load-fbx <path>  Load and play the first animation from an FBX\n"
            "                     file. Both rigid and skinned meshes are\n"
            "                     supported and animated.\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *fbx_path = NULL;
    rt_backend preferred = RT_BACKEND_CPU;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--load-fbx") == 0 && i + 1 < argc) {
            fbx_path = argv[++i];
        } else if (strcmp(argv[i], "-G") == 0 || strcmp(argv[i], "--gpu") == 0) {
            preferred = RT_BACKEND_OPENGL;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "anim: unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int window_w = WINDOW_W, window_h = WINDOW_H;
    SDL_Window *window = SDL_CreateWindow("Anim",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h, SDL_WINDOW_OPENGL);
    if (!window) { fprintf(stderr, "window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) { fprintf(stderr, "gl ctx: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(1);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    rt_renderer *cpu_rnd = rt_renderer_available(RT_BACKEND_CPU)
                         ? rt_renderer_create(RT_BACKEND_CPU)    : NULL;
    rt_renderer *gpu_rnd = rt_renderer_available(RT_BACKEND_OPENGL)
                         ? rt_renderer_create(RT_BACKEND_OPENGL) : NULL;
    if (!cpu_rnd && !gpu_rnd) {
        fprintf(stderr, "no raytrace backend available\n");
        return 1;
    }
    rt_renderer *rnd = (preferred == RT_BACKEND_OPENGL && gpu_rnd) ? gpu_rnd
                     : (cpu_rnd ? cpu_rnd : gpu_rnd);
    fprintf(stderr, "anim: active backend = %s%s\n",
            rt_renderer_name(rnd),
            (cpu_rnd && gpu_rnd) ? " (TAB toggles)" : "");

    scene *s = NULL;
    if (fbx_path) {
        if (!build_scene_from_fbx(fbx_path, &s)) {
            SDL_GL_DeleteContext(gl_ctx);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    } else {
        build_scene(&s);
    }

    /* Auto-frame the orbit camera on the union of every animated pose so
     * a moving rigid mesh stays in view across the full clip; falls back
     * to a generic 5-unit cube if the scene has no mesh-bearing nodes. */
    vector mn, mx;
    int has_aabb = compute_animated_world_aabb(s, &mn, &mx);
    if (!has_aabb) {
        mn = (vector){-2.5f, -2.5f, -2.5f};
        mx = (vector){ 2.5f,  2.5f,  2.5f};
    }
    /* Ground plane for FBX-loaded scenes — placed at the swept min Y so
     * the floor stays below the lowest point of the whole animation,
     * not just the rest pose. The synthetic rig adds its own ground at
     * y=0 inside build_scene. */
    if (fbx_path && has_aabb) {
        int mat_ground = scene_add_material(s, (scene_material){
            .albedo    = {60, 95, 60},
            .albedo2   = {35, 60, 35},
            .tex_kind  = SCENE_TEX_CHECKER,
            .tex_scale = 1.0f,
        });
        scene_add_plane(s, (scene_plane){
            .normal = {0, 1, 0}, .point = {0, mn.y, 0}, .material = mat_ground
        });
    }
    orbit cam_orbit;
    orbit_init_from_aabb(&cam_orbit, mn, mx);
    scene_camera *cam = scene_camera_create((vector){0, 0, 1}, (vector){0, 0, -1});
    orbit_apply(&cam_orbit, cam);

    fprintf(stderr,
            "anim: controls — left-drag orbit, right-drag pan, "
            "scroll zoom, R reset, SPACE pause, TAB toggle backend, ESC quit\n");

    int render_w = window_w, render_h = window_h;
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
    rt_viewport viewport = { render_w, render_h, FOV };

    GLuint tex, fbo;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);

    Uint32 last = SDL_GetTicks();
    Uint32 fps_last = last;
    int fps_frames = 0;
    float anim_time = 0.0f;
    int paused = 0;
    int running = 1;
    char title[160];

    while (running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (e.key.keysym.sym == SDLK_SPACE)  paused = !paused;
                if (e.key.keysym.sym == SDLK_r)      orbit_reset(&cam_orbit);
                if (e.key.keysym.sym == SDLK_TAB && cpu_rnd && gpu_rnd) {
                    rnd = (rnd == cpu_rnd) ? gpu_rnd : cpu_rnd;
                    fprintf(stderr, "anim: backend = %s\n", rt_renderer_name(rnd));
                }
            }
            if (e.type == SDL_MOUSEMOTION) {
                if (e.motion.state & SDL_BUTTON_LMASK) {
                    orbit_drag_orbit(&cam_orbit, e.motion.xrel, e.motion.yrel);
                } else if (e.motion.state & SDL_BUTTON_RMASK) {
                    orbit_drag_pan(&cam_orbit, cam, e.motion.xrel, e.motion.yrel);
                }
            }
            if (e.type == SDL_MOUSEWHEEL) {
                orbit_zoom(&cam_orbit, e.wheel.y);
            }
        }
        orbit_apply(&cam_orbit, cam);

        if (!paused) anim_time += dt;
        if (s->animation_count > 0) {
            scene_anim_sample(s, &s->animations[0], anim_time, 1);
        }

        /* The renderer reads scene->nodes directly: it composes each
         * mesh's world transform from the node hierarchy and transforms
         * the ray into mesh-local space at intersection time. No need
         * to push positions into anything here. */
        rt_renderer_render(rnd, s, cam, &viewport, pixels);
        display_pixels(tex, fbo, pixels, render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        if (now - fps_last >= 1000) {
            snprintf(title, sizeof(title), "Anim — %d FPS %s t=%.2fs",
                     fps_frames, paused ? "[paused]" : "[playing]", anim_time);
            SDL_SetWindowTitle(window, title);
            fps_frames = 0; fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    rt_renderer_destroy(cpu_rnd);
    rt_renderer_destroy(gpu_rnd);
    free(pixels);
    scene_camera_destroy(cam);
    scene_destroy(s);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
