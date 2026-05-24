/* portal_mesh: object-traversal demo, mesh straddling a portal.
 *
 * A flat-shaded octahedron mesh straddles a single paired-rigid disc
 * portal pair (A↔B). The octahedron is co-located with disc A, so its
 * +X half stays visible at the original position and its -X half (the
 * "behind A" half) emerges at B.
 *
 * Camera orbits so you can read the split from several angles. Same
 * multi-portal renderer code that handles spheres also handles meshes
 * — the only difference is the BVH-leaf intersection path. See
 * apps/portal_disc for the analogous sphere demo, and apps/portals for
 * everything integrated. */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "sphere.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 600
#define FOV (M_PI / 2.8f)

/* Flat-shaded octahedron — 8 triangles, 3 unique vertices per face
 * carrying the face normal. Owned by the scene after scene_add_mesh
 * (vertices/indices freed by scene_destroy). */
static scene_mesh make_octahedron_mesh(vector center, float radius,
                                        int material) {
    static const vector base_verts[6] = {
        { 1, 0, 0}, {-1, 0, 0},
        { 0, 1, 0}, { 0,-1, 0},
        { 0, 0, 1}, { 0, 0,-1},
    };
    static const int face_idx[8][3] = {
        {2, 0, 4}, {2, 4, 1}, {2, 1, 5}, {2, 5, 0},
        {3, 4, 0}, {3, 1, 4}, {3, 5, 1}, {3, 0, 5},
    };
    int n_verts = 8 * 3;
    scene_mesh m = {0};
    m.vertices       = malloc(sizeof(scene_vertex) * (size_t)n_verts);
    m.indices        = malloc(sizeof(uint32_t)    * (size_t)n_verts);
    m.vertex_count   = n_verts;
    m.index_count    = n_verts;
    m.material_index = material;
    m.skin_index     = -1;
    int vi = 0;
    for (int f = 0; f < 8; f++) {
        vector v0 = vector_add(center,
                    vector_scale(base_verts[face_idx[f][0]], radius));
        vector v1 = vector_add(center,
                    vector_scale(base_verts[face_idx[f][1]], radius));
        vector v2 = vector_add(center,
                    vector_scale(base_verts[face_idx[f][2]], radius));
        vector n = vector_normalize(
                    vector_cross(vector_sub(v1, v0),
                                  vector_sub(v2, v0)));
        m.vertices[vi+0] = (scene_vertex){.position = v0, .normal = n};
        m.vertices[vi+1] = (scene_vertex){.position = v1, .normal = n};
        m.vertices[vi+2] = (scene_vertex){.position = v2, .normal = n};
        m.indices[vi+0]  = (uint32_t)(vi+0);
        m.indices[vi+1]  = (uint32_t)(vi+1);
        m.indices[vi+2]  = (uint32_t)(vi+2);
        vi += 3;
    }
    scene_mesh_compute_bounds(&m);
    return m;
}

static void build_scene(scene **scene_out, scene_camera **camera_out) {
    scene *sc = scene_create();

    int m_floor = scene_add_material(sc, (scene_material){
        .albedo = {180, 180, 180}, .albedo2 = {60, 60, 60},
        .tex_kind = SCENE_TEX_CHECKER, .tex_scale = 1.0f,
        .portal_index = -1});
    scene_add_plane(sc, (scene_plane){
        .point = {0, -0.5f, 0}, .normal = {0, 1, 0}, .material = m_floor});

    int m_red  = scene_add_material(sc, (scene_material){
        .albedo = {220,  70,  70}, .portal_index = -1});
    int m_blue = scene_add_material(sc, (scene_material){
        .albedo = { 70, 120, 220}, .portal_index = -1});
    scene_add_sphere(sc, (scene_sphere){
        .center = { 0.8f, -0.1f,  3.5f}, .radius = 0.35f, .material = m_red});
    scene_add_sphere(sc, (scene_sphere){
        .center = {-4.0f,  0.2f,  0.5f}, .radius = 0.30f, .material = m_blue});

    int disc_A = 0, disc_B = 1;
    int pA = scene_add_portal(sc, (scene_portal){
        .kind = SCENE_PORTAL_PAIRED_RIGID,
        .partner_kind = SCENE_PRIM_DISC, .partner_index = disc_B});
    int pB = scene_add_portal(sc, (scene_portal){
        .kind = SCENE_PORTAL_PAIRED_RIGID,
        .partner_kind = SCENE_PRIM_DISC, .partner_index = disc_A});
    int mA = scene_add_material(sc, (scene_material){
        .albedo = {200,  80, 230}, .portal_index = pA});
    int mB = scene_add_material(sc, (scene_material){
        .albedo = { 80, 200, 230}, .portal_index = pB});
    scene_add_disc(sc, (scene_disc){
        .center = {-2.0f, 0.5f, -2.0f}, .normal = {1, 0, 0},
        .radius = 1.0f, .material = mA});
    scene_add_disc(sc, (scene_disc){
        .center = {-2.0f, 0.5f,  2.0f}, .normal = {1, 0, 0},
        .radius = 1.0f, .material = mB});

    /* Octahedron co-located with A. Its half on the +X side stays at
     * the original position; its half on the -X side emerges at B. */
    int m_octa = scene_add_material(sc, (scene_material){
        .albedo = {80, 220, 200}, .portal_index = -1});
    scene_mesh octa = make_octahedron_mesh(
        (vector){-2.0f, 0.5f, -2.0f}, 0.6f, m_octa);
    octa.portal_disc1[0] = disc_A + 1;
    scene_add_mesh(sc, octa);

    scene_set_ambient(sc, 0.3f);
    scene_add_light(sc, (scene_light){
        .direction = {0.4f, 0.9f, 0.3f}, .intensity = 0.8f});

    *scene_out  = sc;
    *camera_out = scene_camera_create(
        (vector){4.5f, 1.4f, 1.0f},
        (vector){-1.0f, -0.2f, -0.3f});
}

static vector cam_dir_from_yp(float yaw, float pitch) {
    return (vector){cosf(pitch) * sinf(yaw), sinf(pitch),
                    cosf(pitch) * cosf(yaw)};
}

static void blit(GLuint tex, GLuint fbo, const uint32_t *pixels,
                 int rw, int rh, int ww, int wh) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rw, rh,
                    GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, rw, rh, 0, wh, ww, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int ww = INIT_WINDOW_W, wh = INIT_WINDOW_H;
    SDL_Window *win = SDL_CreateWindow("portal_mesh — octahedron through a disc portal",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, SDL_WINDOW_OPENGL);
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    SDL_GL_SetSwapInterval(1);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    scene *sc; scene_camera *cam;
    build_scene(&sc, &cam);

    int rw = ww / 2, rh = wh / 2;
    uint32_t *px = calloc((size_t)(rw * rh), sizeof(uint32_t));
    rt_viewport vp = {rw, rh, FOV};

    GLuint tex, fbo;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);

    vector cam_pos = {4.5f, 1.4f, 1.0f};
    float cam_yaw = -1.7f, cam_pitch = -0.15f;
    float move_speed = 3.0f, look_speed = 1.6f;
    int auto_orbit = 1, running = 1;
    Uint32 start = SDL_GetTicks(), last = start, fps_t = start;
    int fps_n = 0;
    char title[160];

    while (running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;
        float t = (now - start) / 1000.0f;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (e.key.keysym.sym == SDLK_SPACE)  auto_orbit = !auto_orbit;
            }
        }
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        if (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_S] || k[SDL_SCANCODE_A] ||
            k[SDL_SCANCODE_D] || k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_RIGHT] ||
            k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_DOWN])
            auto_orbit = 0;

        if (auto_orbit) {
            float a = t * 0.22f;
            cam_pos.x = -1.0f + sinf(a) * 5.0f;
            cam_pos.z =          cosf(a) * 5.0f;
            cam_pos.y = 1.4f + sinf(t * 0.4f) * 0.25f;
            vector look = {-2.0f, 0.5f, 0.0f};
            vector dir = vector_normalize(vector_sub(look, cam_pos));
            cam_yaw   = atan2f(dir.x, dir.z);
            cam_pitch = asinf(dir.y);
        } else {
            if (k[SDL_SCANCODE_LEFT])  cam_yaw   -= look_speed * dt;
            if (k[SDL_SCANCODE_RIGHT]) cam_yaw   += look_speed * dt;
            if (k[SDL_SCANCODE_UP])    cam_pitch += look_speed * dt;
            if (k[SDL_SCANCODE_DOWN])  cam_pitch -= look_speed * dt;
            if (cam_pitch >  1.4f) cam_pitch =  1.4f;
            if (cam_pitch < -1.4f) cam_pitch = -1.4f;
            vector fwd = {sinf(cam_yaw), 0, cosf(cam_yaw)};
            vector rgt = {cosf(cam_yaw), 0, -sinf(cam_yaw)};
            if (k[SDL_SCANCODE_W]) cam_pos = vector_add(cam_pos, vector_scale(fwd,  move_speed * dt));
            if (k[SDL_SCANCODE_S]) cam_pos = vector_add(cam_pos, vector_scale(fwd, -move_speed * dt));
            if (k[SDL_SCANCODE_D]) cam_pos = vector_add(cam_pos, vector_scale(rgt,  move_speed * dt));
            if (k[SDL_SCANCODE_A]) cam_pos = vector_add(cam_pos, vector_scale(rgt, -move_speed * dt));
        }
        scene_camera_place(cam, cam_pos, cam_dir_from_yp(cam_yaw, cam_pitch));

        rt_renderer_render(rnd, sc, cam, &vp, px, NULL);
        blit(tex, fbo, px, rw, rh, ww, wh);
        SDL_GL_SwapWindow(win);

        fps_n++;
        if (now - fps_t >= 1000) {
            snprintf(title, sizeof(title),
                     "portal_mesh - %d FPS (%dx%d) %s",
                     fps_n, rw, rh, auto_orbit ? "[orbit]" : "[manual]");
            SDL_SetWindowTitle(win, title);
            fps_n = 0;
            fps_t = now;
        }
    }
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    rt_renderer_destroy(rnd);
    free(px);
    scene_camera_destroy(cam);
    scene_destroy(sc);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
