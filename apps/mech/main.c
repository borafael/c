/* Mech: raytraced scene loaded entirely from a plain-text INI file.
 *
 * Usage: mech [path/to/scene.ini]
 *        (defaults to apps/mech/assets/scene.ini, resolved from CWD)
 *
 * INI schema (all sections optional except at least one light):
 *
 *   [render]     ambient
 *   [camera]     position, look_at
 *   [light.*]    direction, intensity
 *   [material.*] albedo, albedo2, tex_kind, tex_scale, reflectivity, unlit
 *   [plane.*]    point, normal, material
 *   [sphere.*]   center, radius, material
 *   [mesh.*]     file, material, offset, rotation, scale
 *
 * Mesh offset / rotation(euler xyz, degrees) / scale are baked into vertex
 * positions and normals at load time. Materials are referenced by the
 * section's own name (e.g. material=armor resolves to [material.armor]).
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "obj.h"
#include "ini.h"
#include "mesh.h"   /* rt_scene_build_accel */

#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 600
#define FOV (M_PI / 2.8f)
#define RENDER_SCALE_MIN 1
#define RENDER_SCALE_MAX 4

/* ================================ INI parsing helpers ====================== */

static int parse_vec3(const char *s, vector *out) {
    if (!s) return 0;
    return sscanf(s, " %f , %f , %f", &out->x, &out->y, &out->z) == 3
        || sscanf(s, " %f %f %f",     &out->x, &out->y, &out->z) == 3;
}

static int parse_color(const char *s, scene_color *out) {
    if (!s) return 0;
    int r, g, b;
    int ok = sscanf(s, " %d , %d , %d", &r, &g, &b) == 3
          || sscanf(s, " %d %d %d",     &r, &g, &b) == 3;
    if (!ok) return 0;
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    out->r = (uint8_t)r; out->g = (uint8_t)g; out->b = (uint8_t)b;
    return 1;
}

static scene_tex_kind parse_tex_kind(const char *s) {
    if (!s) return SCENE_TEX_NONE;
    struct { const char *name; scene_tex_kind kind; } table[] = {
        {"none",     SCENE_TEX_NONE},
        {"checker",  SCENE_TEX_CHECKER},
        {"image",    SCENE_TEX_IMAGE},
        {"gradient", SCENE_TEX_GRADIENT},
        {"noise",    SCENE_TEX_NOISE},
        {"wood",     SCENE_TEX_WOOD},
        {"marble",   SCENE_TEX_MARBLE},
        {"cells",    SCENE_TEX_CELLS},
        {"cracks",   SCENE_TEX_CRACKS},
        {"stripes",  SCENE_TEX_STRIPES},
        {"dots",     SCENE_TEX_DOTS},
        {"bricks",   SCENE_TEX_BRICKS},
        {"clouds",   SCENE_TEX_CLOUDS},
        {"spots",    SCENE_TEX_SPOTS},
    };
    for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); i++) {
        if (strcasecmp(s, table[i].name) == 0) return table[i].kind;
    }
    fprintf(stderr, "warning: unknown tex_kind '%s'\n", s);
    return SCENE_TEX_NONE;
}

/* Extract "foo" from a section name like "material.foo". Returns NULL if
 * the prefix doesn't match (including the dot). */
static const char *section_suffix(const char *section, const char *prefix) {
    size_t plen = strlen(prefix);
    if (strncmp(section, prefix, plen) != 0) return NULL;
    if (section[plen] != '.') return NULL;
    return section + plen + 1;
}

/* ================================ material name map ======================== */

typedef struct {
    char name[64];
    int  index;
} mat_entry;

typedef struct {
    mat_entry *entries;
    int        count;
    int        capacity;
} mat_map;

static int mat_map_add(mat_map *m, const char *name, int index) {
    if (m->count == m->capacity) {
        int cap = m->capacity ? m->capacity * 2 : 16;
        mat_entry *nd = realloc(m->entries, sizeof(mat_entry) * cap);
        if (!nd) return 0;
        m->entries = nd;
        m->capacity = cap;
    }
    strncpy(m->entries[m->count].name, name, sizeof(m->entries[0].name) - 1);
    m->entries[m->count].name[sizeof(m->entries[0].name) - 1] = '\0';
    m->entries[m->count].index = index;
    m->count++;
    return 1;
}

static int mat_map_lookup(const mat_map *m, const char *name) {
    if (!name) return -1;
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->entries[i].name, name) == 0) return m->entries[i].index;
    }
    return -1;
}

/* ================================ transform bake =========================== */

static void apply_transform_to_mesh(scene_mesh *m,
                                     vector offset,
                                     vector rotation_deg,
                                     vector scale) {
    float rx = rotation_deg.x * (float)M_PI / 180.0f;
    float ry = rotation_deg.y * (float)M_PI / 180.0f;
    float rz = rotation_deg.z * (float)M_PI / 180.0f;
    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);

    /* Rotation matrix (XYZ): R = Rz * Ry * Rx */
    float r00 = cy * cz;
    float r01 = sx * sy * cz - cx * sz;
    float r02 = cx * sy * cz + sx * sz;
    float r10 = cy * sz;
    float r11 = sx * sy * sz + cx * cz;
    float r12 = cx * sy * sz - sx * cz;
    float r20 = -sy;
    float r21 = sx * cy;
    float r22 = cx * cy;

    for (int i = 0; i < m->vertex_count; i++) {
        scene_vertex *v = &m->vertices[i];
        float px = v->position.x * scale.x;
        float py = v->position.y * scale.y;
        float pz = v->position.z * scale.z;
        v->position.x = r00*px + r01*py + r02*pz + offset.x;
        v->position.y = r10*px + r11*py + r12*pz + offset.y;
        v->position.z = r20*px + r21*py + r22*pz + offset.z;

        /* Normals: rotate only (ignore scale for the POC; non-uniform scale
         * would need the inverse-transpose for strictly correct results). */
        float nx = v->normal.x, ny = v->normal.y, nz = v->normal.z;
        v->normal.x = r00*nx + r01*ny + r02*nz;
        v->normal.y = r10*nx + r11*ny + r12*nz;
        v->normal.z = r20*nx + r21*ny + r22*nz;
        float nmag = sqrtf(v->normal.x * v->normal.x
                         + v->normal.y * v->normal.y
                         + v->normal.z * v->normal.z);
        if (nmag > 1e-6f) {
            v->normal.x /= nmag;
            v->normal.y /= nmag;
            v->normal.z /= nmag;
        }
    }
}

/* ================================ scene loader ============================= */

/* Extract the directory portion of path (everything up to and including
 * the last '/'). Writes to dst_buf up to dst_cap - 1 bytes. If there's no
 * slash, dst_buf becomes empty (meaning "current directory"). */
static void path_dirname(const char *path, char *dst_buf, size_t dst_cap) {
    const char *slash = strrchr(path, '/');
    if (!slash) { dst_buf[0] = '\0'; return; }
    size_t n = (size_t)(slash - path + 1);
    if (n >= dst_cap) n = dst_cap - 1;
    memcpy(dst_buf, path, n);
    dst_buf[n] = '\0';
}

static int load_scene_from_ini(const char *ini_path,
                                scene **scene_out,
                                scene_camera **camera_out) {
    ini_file *ini = ini_load(ini_path);
    if (!ini) {
        fprintf(stderr, "error: could not open %s\n", ini_path);
        return 0;
    }

    char base_dir[512];
    path_dirname(ini_path, base_dir, sizeof(base_dir));

    scene *s = scene_create();
    if (!s) { ini_free(ini); return 0; }

    /* Defaults — overridden below. */
    vector cam_pos    = {5.0f, 3.5f, 7.0f};
    vector cam_target = {0.0f, 1.5f, 0.0f};

    mat_map materials = {0};
    int n_sections = ini_section_count(ini);

    /* Two passes: first collect materials (so primitives can reference
     * them by name), then everything else. */
    for (int i = 0; i < n_sections; i++) {
        const char *sect = ini_section_name(ini, i);
        const char *name = section_suffix(sect, "material");
        if (!name) continue;

        scene_material m = scene_material_default();
        scene_color col;
        if (parse_color(ini_get(ini, sect, "albedo"),  &col)) m.albedo  = col;
        if (parse_color(ini_get(ini, sect, "albedo2"), &col)) m.albedo2 = col;
        m.tex_kind     = parse_tex_kind(ini_get(ini, sect, "tex_kind"));
        m.tex_scale    = ini_get_float(ini, sect, "tex_scale",    1.0f);
        m.reflectivity = ini_get_float(ini, sect, "reflectivity", 0.0f);
        m.unlit        = ini_get_bool (ini, sect, "unlit",        0);

        int idx = scene_add_material(s, m);
        if (idx < 0) {
            fprintf(stderr, "error: out of memory adding material '%s'\n", name);
            continue;
        }
        mat_map_add(&materials, name, idx);
    }

    /* Pass two. */
    for (int i = 0; i < n_sections; i++) {
        const char *sect = ini_section_name(ini, i);

        if (strcmp(sect, "render") == 0) {
            scene_set_ambient(s, ini_get_float(ini, sect, "ambient", 0.2f));
        } else if (strcmp(sect, "camera") == 0) {
            vector p;
            if (parse_vec3(ini_get(ini, sect, "position"), &p)) cam_pos    = p;
            if (parse_vec3(ini_get(ini, sect, "look_at"),  &p)) cam_target = p;
        } else if (section_suffix(sect, "light")) {
            vector dir = {0, 1, 0};
            parse_vec3(ini_get(ini, sect, "direction"), &dir);
            scene_light L = {
                .direction = dir,
                .intensity = ini_get_float(ini, sect, "intensity", 0.5f),
            };
            scene_add_light(s, L);
        } else if (section_suffix(sect, "plane")) {
            vector pt = {0,0,0}, nrm = {0,1,0};
            parse_vec3(ini_get(ini, sect, "point"),  &pt);
            parse_vec3(ini_get(ini, sect, "normal"), &nrm);
            int mat = mat_map_lookup(&materials, ini_get(ini, sect, "material"));
            if (mat < 0) {
                fprintf(stderr, "warning: [%s] material not found; skipping\n", sect);
                continue;
            }
            scene_add_plane(s, (scene_plane){ .point = pt, .normal = nrm, .material = mat });
        } else if (section_suffix(sect, "sphere")) {
            vector c = {0,0,0};
            parse_vec3(ini_get(ini, sect, "center"), &c);
            float r = ini_get_float(ini, sect, "radius", 1.0f);
            int mat = mat_map_lookup(&materials, ini_get(ini, sect, "material"));
            if (mat < 0) {
                fprintf(stderr, "warning: [%s] material not found; skipping\n", sect);
                continue;
            }
            scene_add_sphere(s, (scene_sphere){ .center = c, .radius = r, .material = mat });
        } else if (section_suffix(sect, "mesh")) {
            const char *file = ini_get(ini, sect, "file");
            if (!file) {
                fprintf(stderr, "warning: [%s] missing file; skipping\n", sect);
                continue;
            }
            const char *mtl_file = ini_get(ini, sect, "mtl");
            int mat = mat_map_lookup(&materials, ini_get(ini, sect, "material"));
            if (mat < 0 && !mtl_file) {
                fprintf(stderr, "warning: [%s] needs 'material' or 'mtl'; skipping\n", sect);
                continue;
            }

            char obj_full[1024];
            snprintf(obj_full, sizeof(obj_full), "%s%s", base_dir, file);

            /* Optional MTL — when present, its materials get added to the
             * scene and resolve usemtl groups in the OBJ. `material` in the
             * INI still acts as the fallback for unknown / missing groups. */
            scene_mtl_entry *mtl_entries = NULL;
            int mtl_count = 0;
            if (mtl_file) {
                char mtl_full[1024];
                snprintf(mtl_full, sizeof(mtl_full), "%s%s", base_dir, mtl_file);
                mtl_count = scene_load_mtl(mtl_full, &mtl_entries);
                if (mtl_count < 0) {
                    fprintf(stderr, "warning: [%s] failed to load mtl '%s'\n",
                            sect, mtl_full);
                    mtl_count = 0;
                    mtl_entries = NULL;
                }
            }

            int first_mesh = 0;
            int default_mat = (mat >= 0) ? mat : 0;  /* safe index into scene->materials */
            int added = scene_add_meshes_from_obj(s, obj_full,
                                                  mtl_entries, mtl_count,
                                                  default_mat, &first_mesh);
            free(mtl_entries);

            if (added <= 0) {
                fprintf(stderr, "warning: [%s] failed to load '%s'; skipping\n",
                        sect, obj_full);
                continue;
            }

            vector offset = {0,0,0}, rot = {0,0,0}, scl = {1,1,1};
            parse_vec3(ini_get(ini, sect, "offset"),   &offset);
            parse_vec3(ini_get(ini, sect, "rotation"), &rot);
            const char *scale_str = ini_get(ini, sect, "scale");
            if (scale_str) {
                if (!parse_vec3(scale_str, &scl)) {
                    float uniform;
                    if (sscanf(scale_str, " %f", &uniform) == 1) {
                        scl = (vector){uniform, uniform, uniform};
                    }
                }
            }
            for (int k = 0; k < added; k++) {
                scene_mesh *sm = &s->meshes[first_mesh + k];
                apply_transform_to_mesh(sm, offset, rot, scl);
                scene_mesh_compute_bounds(sm);
            }
        }
    }

    free(materials.entries);
    ini_free(ini);

    /* If the INI didn't register any lights, ensure the scene isn't pitch-black. */
    if (s->light_count == 0) {
        scene_add_light(s, (scene_light){ .direction = {0.3f, 0.9f, 0.3f}, .intensity = 0.8f });
    }

    /* Build per-mesh BVHs after all transforms are baked. The CPU
     * ray-mesh test uses these; linear scan stays the fallback. */
    rt_scene_build_accel(s);

    vector forward = vector_normalize(vector_sub(cam_target, cam_pos));
    *scene_out  = s;
    *camera_out = scene_camera_create(cam_pos, forward);
    return 1;
}

/* ================================ SDL app (based on apps/orb) ============== */

static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw)
    };
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

int main(int argc, char *argv[]) {
    const char *ini_path = "apps/mech/assets/scene.ini";
    int auto_orbit = 1;
    rt_backend preferred = RT_BACKEND_CPU;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [-G] [path/to/scene.ini]\n", argv[0]);
            printf("  -G, --gpu    Start with OpenGL backend (falls back to CPU)\n");
            printf("  ESC quit, TAB toggle backend, O toggle orbit, WASD+arrows fly,\n");
            printf("  SPACE/LSHIFT up/down, F11 fullscreen, +/- change render resolution.\n");
            return 0;
        }
        if (strcmp(argv[i], "-G") == 0 || strcmp(argv[i], "--gpu") == 0) {
            preferred = RT_BACKEND_OPENGL;
            continue;
        }
        ini_path = argv[i];
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int window_w = INIT_WINDOW_W, window_h = INIT_WINDOW_H, fullscreen = 0;
    SDL_Window *window = SDL_CreateWindow("Mech",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h, SDL_WINDOW_OPENGL);
    if (!window) { fprintf(stderr, "Window creation failed\n"); SDL_Quit(); return 1; }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(1);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    rt_renderer *cpu_rnd = rt_renderer_available(RT_BACKEND_CPU)
                         ? rt_renderer_create(RT_BACKEND_CPU) : NULL;
    rt_renderer *gpu_rnd = rt_renderer_available(RT_BACKEND_OPENGL)
                         ? rt_renderer_create(RT_BACKEND_OPENGL) : NULL;
    if (!cpu_rnd && !gpu_rnd) {
        fprintf(stderr, "No renderers available\n");
        return 1;
    }
    rt_renderer *active = (preferred == RT_BACKEND_OPENGL && gpu_rnd) ? gpu_rnd
                        : (cpu_rnd ? cpu_rnd : gpu_rnd);
    fprintf(stderr, "Active: %s (TAB toggles backend)\n", rt_renderer_name(active));

    scene *scene;
    scene_camera *camera;
    if (!load_scene_from_ini(ini_path, &scene, &camera)) {
        rt_renderer_destroy(active);
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int render_scale = 2;
    int render_w = window_w / render_scale;
    int render_h = window_h / render_scale;
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
    rt_viewport viewport = { render_w, render_h, FOV };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    /* Orbit around the initial look-at point (inferred from camera basis). */
    vector cam_origin_init, cam_fwd_init, cam_right_init, cam_up_init;
    scene_camera_get_basis(camera, &cam_origin_init, &cam_fwd_init,
                           &cam_right_init, &cam_up_init);
    vector pivot = vector_add(cam_origin_init, vector_scale(cam_fwd_init, 6.0f));
    vector orbit_rel = vector_sub(cam_origin_init, pivot);
    float orbit_radius = sqrtf(orbit_rel.x*orbit_rel.x
                              + orbit_rel.z*orbit_rel.z);
    if (orbit_radius < 0.5f) orbit_radius = 6.0f;
    float orbit_height = orbit_rel.y;
    float orbit_speed = 0.35f;

    vector cam_pos = cam_origin_init;
    float cam_yaw = atan2f(cam_fwd_init.x, cam_fwd_init.z);
    float cam_pitch = asinf(cam_fwd_init.y);
    float move_speed = 3.5f;
    float look_speed = 1.8f;

    Uint32 fps_last = SDL_GetTicks();
    Uint32 frame_last = SDL_GetTicks();
    Uint32 start_ticks = SDL_GetTicks();
    int fps_frames = 0;
    Uint32 render_ms_accum = 0;
    char title_buf[200];
    int running = 1;

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        frame_last = frame_now;
        float t = (frame_now - start_ticks) / 1000.0f;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (e.key.keysym.sym == SDLK_TAB) {
                    if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                    else if (active == gpu_rnd && cpu_rnd) active = cpu_rnd;
                    fprintf(stderr, "Active: %s\n", rt_renderer_name(active));
                }
                if (e.key.keysym.sym == SDLK_o) {
                    auto_orbit = !auto_orbit;
                    fprintf(stderr, "Orbit: %s\n", auto_orbit ? "on" : "off");
                }
                if (e.key.keysym.sym == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_GetWindowSize(window, &window_w, &window_h);
                    goto recreate_buffers;
                }
                if (e.key.keysym.sym == SDLK_MINUS ||
                    e.key.keysym.sym == SDLK_KP_MINUS) {
                    if (render_scale < RENDER_SCALE_MAX) {
                        render_scale++;
                        goto recreate_buffers;
                    }
                }
                if (e.key.keysym.sym == SDLK_EQUALS ||
                    e.key.keysym.sym == SDLK_KP_PLUS) {
                    if (render_scale > RENDER_SCALE_MIN) {
                        render_scale--;
                        goto recreate_buffers;
                    }
                }
                continue;
                recreate_buffers:
                    render_w = window_w / render_scale;
                    render_h = window_h / render_scale;
                    free(pixels);
                    pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
                    viewport = (rt_viewport){ render_w, render_h, FOV };
                    glBindTexture(GL_TEXTURE_2D, display_tex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
                    fprintf(stderr, "Render scale: 1/%d (%dx%d)\n",
                            render_scale, render_w, render_h);
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        int manual_input = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_S] ||
                           keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_D] ||
                           keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT] ||
                           keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_DOWN] ||
                           keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] ||
                           keys[SDL_SCANCODE_SPACE];
        if (manual_input) auto_orbit = 0;

        if (auto_orbit) {
            float a = t * orbit_speed;
            cam_pos.x = pivot.x + cosf(a) * orbit_radius;
            cam_pos.z = pivot.z + sinf(a) * orbit_radius;
            cam_pos.y = pivot.y + orbit_height + sinf(t * 0.35f) * 0.5f;
            vector dir = vector_normalize(vector_sub(pivot, cam_pos));
            cam_yaw   = atan2f(dir.x, dir.z);
            cam_pitch = asinf(dir.y);
        } else {
            if (keys[SDL_SCANCODE_LEFT])  cam_yaw   -= look_speed * dt;
            if (keys[SDL_SCANCODE_RIGHT]) cam_yaw   += look_speed * dt;
            if (keys[SDL_SCANCODE_UP])    cam_pitch += look_speed * dt;
            if (keys[SDL_SCANCODE_DOWN])  cam_pitch -= look_speed * dt;
            if (cam_pitch >  1.4f) cam_pitch =  1.4f;
            if (cam_pitch < -1.4f) cam_pitch = -1.4f;

            vector forward = { sinf(cam_yaw), 0.0f, cosf(cam_yaw) };
            vector right   = { cosf(cam_yaw), 0.0f, -sinf(cam_yaw) };
            if (keys[SDL_SCANCODE_W]) cam_pos = vector_add(cam_pos, vector_scale(forward,  move_speed * dt));
            if (keys[SDL_SCANCODE_S]) cam_pos = vector_add(cam_pos, vector_scale(forward, -move_speed * dt));
            if (keys[SDL_SCANCODE_D]) cam_pos = vector_add(cam_pos, vector_scale(right,    move_speed * dt));
            if (keys[SDL_SCANCODE_A]) cam_pos = vector_add(cam_pos, vector_scale(right,   -move_speed * dt));
            if (keys[SDL_SCANCODE_SPACE])  cam_pos.y += move_speed * dt;
            if (keys[SDL_SCANCODE_LSHIFT] ||
                keys[SDL_SCANCODE_RSHIFT]) cam_pos.y -= move_speed * dt;
        }

        vector cam_dir = cam_dir_from_yaw_pitch(cam_yaw, cam_pitch);
        scene_camera_place(camera, cam_pos, cam_dir);

        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, scene, camera, &viewport, pixels, NULL);
        render_ms_accum += SDL_GetTicks() - r_start;

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float avg_ms = (fps_frames > 0)
                ? (float)render_ms_accum / (float)fps_frames : 0.0f;
            snprintf(title_buf, sizeof(title_buf),
                     "Mech - %d FPS (%.2f ms, %dx%d, 1/%d) %s",
                     fps_frames, avg_ms,
                     render_w, render_h, render_scale,
                     auto_orbit ? "[orbit]" : "[manual]");
            SDL_SetWindowTitle(window, title_buf);
            fps_frames = 0;
            render_ms_accum = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    free(pixels);
    scene_camera_destroy(camera);
    scene_destroy(scene);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
