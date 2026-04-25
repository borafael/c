/* Anim: end-to-end test of scene_animation + scene_anim_sample.
 *
 * Builds a three-segment "arm" (shoulder → elbow → wrist) as rigid nodes
 * in the scene's node tree. Each node also owns a primitive sphere whose
 * center is repositioned each frame from the node's resolved world
 * transform. A synthetic 2-second scene_animation swings the shoulder
 * and bends the elbow around Z.
 *
 * Why spheres and not meshes: the raytracer doesn't consume scene_node
 * yet (commit 2148edc added the data model only). The app resolves node
 * world transforms itself and pushes them into primitive positions the
 * renderer already understands. This validates the animation data path
 * (sampling + parent composition) without needing a mesh-transform pass
 * in the renderer. Controls: ESC quits, SPACE pauses.
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include "plane.h"

#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_W     960
#define WINDOW_H     600
#define FOV          (M_PI / 3.0f)
#define NUM_SEGMENTS 3

static int seg_node[NUM_SEGMENTS];     /* scene->nodes   index per arm segment */
static int seg_sphere[NUM_SEGMENTS];   /* scene->spheres index per arm segment */

/* Rotate a vector by Euler XYZ (radians), applying Z then Y then X.
 * Matches the rotation order scene_anim_sample writes into the transform. */
static vector rotate_euler(vector p, vector e) {
    float sx = sinf(e.x), cx = cosf(e.x);
    float sy = sinf(e.y), cy = cosf(e.y);
    float sz = sinf(e.z), cz = cosf(e.z);
    vector a = { p.x * cz - p.y * sz,  p.x * sz + p.y * cz,  p.z };
    vector b = { a.x * cy + a.z * sy,  a.y,                 -a.x * sy + a.z * cy };
    vector c = { b.x,                   b.y * cx - b.z * sx, b.y * sx + b.z * cx };
    return c;
}

/* Forward pass: fill world_pos/world_rot for every node.
 * Nodes are guaranteed parent-before-child, so one linear sweep is
 * enough. Rotations are summed channel-wise — exact when every rotation
 * in the chain is around the same axis (our demo rotates only around
 * Z); an approximation otherwise. A proper matrix/quat composition
 * can drop in here later without touching callers. */
static void resolve_world_transforms(const scene *s,
                                     vector *world_pos,
                                     vector *world_rot) {
    for (int i = 0; i < s->node_count; i++) {
        const scene_node *n = &s->nodes[i];
        if (n->parent_index < 0) {
            world_pos[i] = n->transform.position;
            world_rot[i] = n->transform.rotation;
        } else {
            vector pwr = world_rot[n->parent_index];
            vector pwp = world_pos[n->parent_index];
            world_pos[i] = vector_add(pwp, rotate_euler(n->transform.position, pwr));
            world_rot[i] = vector_add(pwr, n->transform.rotation);
        }
    }
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

static void build_scene(scene **scene_out, scene_camera **camera_out) {
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

    for (int i = 0; i < NUM_SEGMENTS; i++) {
        int mat = scene_add_material(s, (scene_material){
            .albedo = colors[i], .reflectivity = 0.1f
        });
        seg_sphere[i] = scene_add_sphere(s, (scene_sphere){
            .center = local_offset[i], .radius = radii[i], .material = mat
        });
        scene_node n;
        memset(n.name, 0, sizeof(n.name));
        strncpy(n.name, names[i], sizeof(n.name) - 1);
        n.transform    = scene_transform_identity();
        n.transform.position = local_offset[i];
        n.parent_index = parents[i];
        n.mesh_index   = -1;
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

    *scene_out  = s;
    *camera_out = scene_camera_create(
        (vector){5.5f, 3.2f, 6.5f},
        (vector){-0.55f, -0.10f, -0.82f}
    );
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

int main(void) {
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

    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    if (!rnd) { fprintf(stderr, "no CPU backend\n"); return 1; }

    scene *s;
    scene_camera *cam;
    build_scene(&s, &cam);

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

    vector world_pos[32], world_rot[32];

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
            }
        }

        if (!paused) anim_time += dt;
        scene_anim_sample(s, &s->animations[0], anim_time, 1);

        /* Resolve world transforms from the (now animated) locals and
         * push each segment's world position into its primitive sphere. */
        resolve_world_transforms(s, world_pos, world_rot);
        for (int i = 0; i < NUM_SEGMENTS; i++) {
            s->spheres[seg_sphere[i]].center = world_pos[seg_node[i]];
        }

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
    rt_renderer_destroy(rnd);
    free(pixels);
    scene_camera_destroy(cam);
    scene_destroy(s);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
