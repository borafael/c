/* Alpine Lake — detailed mountain landscape with reflective lake.
 *
 * A cohesive scene built from noise-generated heightfield terrain,
 * reflective water, pine trees (cone + cylinder), a small cabin
 * (boxes), rocks (spheres), and a warm sun. Post-processing adds
 * atmospheric fog, bloom, and cinematic vignette.
 *
 * Primitives:  heightfield (terrain), plane (water), cylinder (trunks),
 *              cone (canopies), box (cabin), sphere (sun + rocks)
 * Textures:    noise (terrain), wood (trunks/cabin), gradient (sun)
 * PostFX:      bloom + fog + vignette
 *
 * Controls:
 *   ESC        quit
 *   TAB        toggle CPU / OpenGL backend
 *   WASD       fly forward/back/strafe
 *   Space      fly up
 *   Shift      fly down
 *   Arrows     look around
 *   F11        fullscreen
 *   1..4       resolution preset
 *   P          toggle all post-processing
 *   B          toggle bloom
 *   F          toggle depth fog
 *   V          toggle vignette
 *   - / =      bloom intensity
 *   [ / ]      bloom radius
 *   H          print help controls
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "mesh.h"
#include "postfx.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 600
#define FOV (M_PI / 2.8f)
#define WALK_SPEED 5.0f
#define LOOK_SPEED 2.0f
#define PITCH_LIMIT 1.4f

static const struct { int w, h; const char *name; } PRESETS[] = {
    { 320,  200, "320x200" },
    { 480,  300, "480x300" },
    { 640,  400, "640x400" },
    { 960,  600, "960x600" },
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
#define PRESET_DEFAULT 3

static float smoothstep(float edge0, float edge1, float x) {
    float t = fmaxf(0.0f, fminf(1.0f, (x - edge0) / (edge1 - edge0)));
    return t * t * (3.0f - 2.0f * t);
}

static float hash2(float x, float y) {
    return fabsf(sinf(x * 127.1f + y * 311.7f));
}

static float value_noise(float x, float y) {
    int ix = (int)floorf(x);
    int iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float a = hash2(ix, iy);
    float b = hash2(ix + 1, iy);
    float c = hash2(ix, iy + 1);
    float d = hash2(ix + 1, iy + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static float fbm(float x, float y, int octaves) {
    float v = 0.0f, amp = 1.0f, freq = 1.0f, max_amp = 0.0f;
    for (int i = 0; i < octaves; i++) {
        v += amp * value_noise(x * freq, y * freq);
        max_amp += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return v / max_amp;
}

static float terrain_height(float x, float z) {
    float n = fbm(x * 0.025f, z * 0.025f, 6);
    float ridge = 1.0f - fabsf(fbm(x * 0.04f + 10.0f, z * 0.04f, 4) - 0.5f) * 2.0f;
    ridge = fmaxf(0.0f, ridge);
    ridge = ridge * ridge * ridge;

    float z_mtn = smoothstep(3.0f, 20.0f, z);
    float z_valley = 1.0f - smoothstep(-8.0f, 8.0f, fabsf(z));
    float z_hill = 1.0f - smoothstep(-25.0f, -5.0f, z);

    float mtn = n * (2.0f + ridge * 4.0f) * z_mtn;
    float valley = (n - 0.5f) * 0.6f * z_valley;
    float hill = n * 1.5f * z_hill;
    float h = mtn + valley + hill - 0.3f;
    return fmaxf(h, -1.0f);
}

static void generate_terrain(float *heights, float *normals,
                              int rows, int cols,
                              float world_w, float world_d,
                              float ox, float oz) {
    float cell_w = world_w / (cols - 1);
    float cell_d = world_d / (rows - 1);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            float wx = ox + c * cell_w;
            float wz = oz + r * cell_d;
            heights[r * cols + c] = terrain_height(wx, wz);
        }
    }
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int il = c > 0 ? c - 1 : c + 1;
            int ir = c < cols - 1 ? c + 1 : c - 1;
            int id = r > 0 ? r - 1 : r + 1;
            int iu = r < rows - 1 ? r + 1 : r - 1;
            float hL = heights[r * cols + il];
            float hR = heights[r * cols + ir];
            float hD = heights[id * cols + c];
            float hU = heights[iu * cols + c];
            vector t = {2.0f * cell_w, hR - hL, 0.0f};
            vector b = {0.0f, hU - hD, 2.0f * cell_d};
            vector n = vector_normalize(vector_cross(b, t));
            int idx = (r * cols + c) * 3;
            normals[idx] = n.x; normals[idx + 1] = n.y; normals[idx + 2] = n.z;
        }
    }
}

static void draw_tree(scene *s, float x, float z, float scale,
                       int m_trunk, int m_canopy) {
    float h = scale * (0.6f + fbm(x * 0.5f, z * 0.5f, 2) * 0.4f);
    float trunk_h = h * 0.4f;
    float trunk_r = scale * 0.08f;
    float canopy_r = scale * (0.25f + h * 0.06f);
    scene_add_cylinder(s, (scene_cylinder){
        .center = {x, trunk_h * 0.5f, z},
        .axis = {0, 1, 0},
        .radius = trunk_r,
        .half_height = trunk_h * 0.5f,
        .material = m_trunk,
    });
    scene_add_cone(s, (scene_cone){
        .apex = {x, trunk_h + h * 0.5f, z},
        .axis = {0, -1, 0},
        .height = h * 0.6f,
        .radius = canopy_r,
        .material = m_canopy,
    });
}

static void draw_cabin(scene *s, float bx, float by, float bz, float angle,
                        int m_wall, int m_roof, int m_trim) {
    float cw = 2.0f, cd = 2.4f, ch = 1.2f;
    float c = cosf(angle), sn = sinf(angle);
    vector mt(float px, float pz) {
        return (vector){bx + px * c - pz * sn, by, bz + px * sn + pz * c};
    }
    float min_x = -cw * 0.5f, max_x = cw * 0.5f;
    float min_z = -cd * 0.5f, max_z = cd * 0.5f;

    vector w_min = mt(min_x, min_z);
    vector w_max = mt(max_x, max_z);

    scene_add_box(s, scene_box_aabb(
        (vector){w_min.x, by, w_min.z},
        (vector){w_max.x, by + ch, w_max.z}, m_wall
    ));
    float roof_h = 0.8f, rw = cw * 0.5f + 0.3f;
    vector rr = mt(0.0f, min_z - 0.3f);
    vector rf = mt(0.0f, min_z + 0.1f);
    vector rl = mt(-rw, 0.0f);
    vector rl2 = mt(rw, 0.0f);
    scene_add_triangle(s, (scene_triangle){
        .v0 = {rr.x, by + ch, rr.z},
        .v1 = {rl.x, by + ch, rl.z},
        .v2 = {rl2.x, by + ch, rl2.z},
        .material = m_roof,
    });
    scene_add_triangle(s, (scene_triangle){
        .v0 = {rf.x, by + ch, rf.z},
        .v1 = {rl2.x, by + ch, rl2.z},
        .v2 = {rl.x, by + ch, rl.z},
        .material = m_roof,
    });
    scene_add_box(s, scene_box_aabb(
        (vector){rr.x, by + ch, rr.z},
        (vector){rf.x, by + ch + roof_h, rf.z}, m_roof
    ));
    float dr = 0.15f;
    vector dd = mt(0.4f, min_z + 0.05f);
    scene_add_box(s, scene_box_aabb(
        (vector){dd.x - dr, by + 0.05f, dd.z - dr},
        (vector){dd.x + dr, by + 0.55f, dd.z + dr}, m_trim
    ));
    vector wd = mt(0.25f, min_z + 0.25f);
    scene_add_box(s, scene_box_aabb(
        (vector){wd.x - 0.02f, by + 0.25f, wd.z - 0.02f},
        (vector){wd.x + 0.02f, by + 0.40f, wd.z + 0.02f}, m_trim
    ));
}

static void build_scene(scene **scn_out, scene_camera **cam_out) {
    scene *s = scene_create();

    int m_ground = scene_add_material(s, (scene_material){
        .albedo    = {110, 165, 90},
        .albedo2   = {60, 100, 50},
        .tex_kind  = SCENE_TEX_NOISE,
        .tex_scale = 2.0f,
    });
    int m_water = scene_add_material(s, (scene_material){
        .albedo       = {30, 80, 130},
        .albedo2      = {20, 60, 110},
        .tex_kind     = SCENE_TEX_NOISE,
        .tex_scale    = 4.0f,
        .reflectivity = 0.35f,
    });
    int m_mtn_rock = scene_add_material(s, (scene_material){
        .albedo    = {140, 130, 120},
        .albedo2   = {100, 90, 80},
        .tex_kind  = SCENE_TEX_CRACKS,
        .tex_scale = 3.0f,
    });
    int m_trunk = scene_add_material(s, (scene_material){
        .albedo    = {110, 70, 40},
        .albedo2   = {70, 45, 25},
        .tex_kind  = SCENE_TEX_WOOD,
        .tex_scale = 0.4f,
    });
    int m_canopy = scene_add_material(s, (scene_material){
        .albedo    = {50, 130, 45},
        .albedo2   = {25, 80, 20},
        .tex_kind  = SCENE_TEX_NOISE,
        .tex_scale = 0.3f,
    });
    int m_sun = scene_add_material(s, (scene_material){
        .albedo    = {255, 240, 200},
        .albedo2   = {255, 200, 120},
        .tex_kind  = SCENE_TEX_GRADIENT,
        .tex_scale = 1.0f,
        .unlit     = 1,
    });
    int m_cabin_wall = scene_add_material(s, (scene_material){
        .albedo    = {150, 110, 70},
        .albedo2   = {110, 80, 50},
        .tex_kind  = SCENE_TEX_WOOD,
        .tex_scale = 0.3f,
    });
    int m_cabin_roof = scene_add_material(s, (scene_material){
        .albedo  = {130, 55, 40},
        .albedo2 = {90, 35, 25},
        .tex_kind = SCENE_TEX_STRIPES,
        .tex_scale = 0.2f,
    });
    int m_cabin_trim = scene_add_material(s, (scene_material){
        .albedo = {60, 60, 65},
    });
    int m_rock = scene_add_material(s, (scene_material){
        .albedo    = {120, 115, 105},
        .albedo2   = {90, 85, 75},
        .tex_kind  = SCENE_TEX_SPOTS,
        .tex_scale = 0.5f,
    });

#define HF_ROWS 40
#define HF_COLS 40
#define HF_WW 70.0f
#define HF_WD 70.0f
    static float hf_heights[HF_ROWS * HF_COLS];
    static float hf_normals[HF_ROWS * HF_COLS * 3];
    static uint8_t hf_colors[(HF_ROWS - 1) * (HF_COLS - 1) * 3];

    generate_terrain(hf_heights, hf_normals, HF_ROWS, HF_COLS,
                     HF_WW, HF_WD, -HF_WW * 0.5f, -HF_WD * 0.5f);

    float water_y = 0.0f;
    for (int r = 0; r < HF_ROWS - 1; r++) {
        for (int c = 0; c < HF_COLS - 1; c++) {
            float h = hf_heights[r * HF_COLS + c];
            float h_n = hf_heights[(r + 1) * HF_COLS + c];
            float h_e = hf_heights[r * HF_COLS + (c + 1)];
            float avg = (h + h_n + h_e) / 3.0f;
            int is_rock = avg > 1.5f;
            int is_shore = avg > -0.1f && avg < 0.4f;
            int i = (r * (HF_COLS - 1) + c) * 3;
            if (is_rock) {
                hf_colors[i]     = (uint8_t)(140 + (int)(h * 15) % 30);
                hf_colors[i + 1] = (uint8_t)(130 + (int)(h * 10) % 20);
                hf_colors[i + 2] = (uint8_t)(120 + (int)(h * 8) % 15);
            } else if (is_shore) {
                hf_colors[i]     = 160;
                hf_colors[i + 1] = 150;
                hf_colors[i + 2] = 100;
            } else {
                float g = 100.0f + (avg + 0.5f) * 60.0f;
                hf_colors[i]     = (uint8_t)(g * 0.7f);
                hf_colors[i + 1] = (uint8_t)(g);
                hf_colors[i + 2] = (uint8_t)(g * 0.5f);
            }
        }
    }

    scene_add_heightfield(s, &(scene_heightfield){
        .heights     = hf_heights,
        .colors      = hf_colors,
        .normals     = hf_normals,
        .rows        = HF_ROWS,
        .cols        = HF_COLS,
        .world_width = HF_WW,
        .world_depth = HF_WD,
        .origin_x    = -HF_WW * 0.5f,
        .origin_z    = -HF_WD * 0.5f,
        .max_height  = 8.0f,
        .material    = m_ground,
    });

    scene_add_plane(s, (scene_plane){
        .point    = {0, water_y, 0},
        .normal   = {0, 1, 0},
        .material = m_water,
    });

    scene_add_sphere(s, (scene_sphere){
        .center   = {12.0f, 28.0f, 22.0f},
        .radius   = 2.5f,
        .material = m_sun,
    });

    int m_mtn_ground = m_ground;
    (void)m_mtn_rock;
    (void)m_mtn_ground;

    float tree_pos[][2] = {
        {-12, -4}, {-8, -6}, {-4, -3}, {0, -5},
        {5, -4}, {9, -6}, {13, -3}, {16, -5},
        {-14, 2}, {-10, 4}, {-6, 1}, {-2, 3},
        {3, 2}, {7, 4}, {11, 1}, {15, 3},
        {-13, -10}, {-7, -12}, {2, -10}, {10, -11},
    };
    int num_trees = sizeof(tree_pos) / sizeof(tree_pos[0]);
    for (int i = 0; i < num_trees; i++) {
        float tx = tree_pos[i][0], tz = tree_pos[i][1];
        float th = terrain_height(tx, tz);
        if (th > -0.2f && th < 1.5f) {
            float sv = 0.8f + fbm(tx * 0.3f, tz * 0.3f, 3) * 0.6f;
            draw_tree(s, tx, tz, sv, m_trunk, m_canopy);

        }
    }

    float cabin_x = -18.0f, cabin_z = 3.5f;
    float ch = terrain_height(cabin_x, cabin_z);
    if (ch > -0.1f && ch < 1.2f) {
        draw_cabin(s, cabin_x, ch, cabin_z, 0.3f,
                   m_cabin_wall, m_cabin_roof, m_cabin_trim);
    }

    float rock_pos[][3] = {
        {-16, 5.5f, 0.6f}, {-14, 8.0f, 0.8f}, {-10, 6.5f, 0.4f},
        {18, -2.0f, 0.7f}, {20, 1.0f, 1.0f}, {22, -3.5f, 0.5f},
        {-22, 12.0f, 0.9f},
    };
    for (int i = 0; i < 7; i++) {
        float rx = rock_pos[i][0], rz = rock_pos[i][1], rr = rock_pos[i][2];
        float rh = terrain_height(rx, rz);
        scene_add_sphere(s, (scene_sphere){
            .center   = {rx, rh + rr * 0.5f, rz},
            .radius   = rr,
            .material = m_rock,
        });
    }

    scene_set_ambient(s, 0.12f);
    scene_add_light(s, (scene_light){
        .direction = {0.5f, 0.9f, -0.3f},
        .intensity = 0.8f,
    });
    scene_add_light(s, (scene_light){
        .direction = {-0.4f, 0.3f, -0.5f},
        .intensity = 0.25f,
    });
    scene_add_light(s, (scene_light){
        .direction = {0.0f, -0.2f, 0.8f},
        .intensity = 0.10f,
    });

    *scn_out = s;
    *cam_out = scene_camera_create(
        (vector){-5.0f, 3.0f, -16.0f},
        (vector){0.25f, -0.15f, 1.0f}
    );
}

static vector cam_dir_from_yaw_pitch(float yaw, float pitch) {
    return (vector){
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw),
    };
}

static void print_help(void) {
    fprintf(stderr,
        "\n=== Alpine Lake Controls ===\n"
        "  ESC        quit\n"
        "  TAB        toggle CPU / OpenGL backend\n"
        "  WASD       fly forward/back/strafe\n"
        "  Space      fly up\n"
        "  Shift      fly down\n"
        "  Arrows     look around\n"
        "  F11        fullscreen\n"
        "  1..4       resolution preset\n"
        "  P          toggle all post-processing\n"
        "  B          toggle bloom\n"
        "  F          toggle depth fog\n"
        "  V          toggle vignette\n"
        "  - / =      bloom intensity\n"
        "  [ / ]      bloom radius\n"
        "  H          this help\n"
        "============================\n\n");
}

static void fill_sky(uint32_t *pixels, int w, int h) {
    for (int y = 0; y < h; y++) {
        float t = (float)y / h;
        float warm = 1.0f - t;
        float blue = t;
        int r = (int)(180.0f + warm * 75.0f);
        int g = (int)(160.0f + warm * 50.0f + blue * 40.0f);
        int b = (int)(130.0f + blue * 120.0f);
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
        for (int x = 0; x < w; x++) {
            pixels[y * w + x] = color;
        }
    }
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
    rt_backend preferred = RT_BACKEND_CPU;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-G") == 0 || strcmp(argv[i], "--gpu") == 0)
            preferred = RT_BACKEND_OPENGL;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("  -G, --gpu    Start with OpenGL raytrace backend\n");
            printf("  -h, --help   Show this help\n");
            return 0;
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

    int window_w = INIT_WINDOW_W, window_h = INIT_WINDOW_H;
    int fullscreen = 0;
    SDL_Window *window = SDL_CreateWindow("Alpine Lake",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h, SDL_WINDOW_OPENGL);
    if (!window) {
        fprintf(stderr, "Window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(0);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);

    fprintf(stderr, "GL version: %s\n", (const char *)glGetString(GL_VERSION));

    rt_renderer *cpu_rnd = rt_renderer_available(RT_BACKEND_CPU)
                         ? rt_renderer_create(RT_BACKEND_CPU) : NULL;
    rt_renderer *gpu_rnd = rt_renderer_available(RT_BACKEND_OPENGL)
                         ? rt_renderer_create(RT_BACKEND_OPENGL) : NULL;
    if (!cpu_rnd && !gpu_rnd) {
        fprintf(stderr, "No renderers available\n");
        SDL_GL_DeleteContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    rt_renderer *active = (preferred == RT_BACKEND_OPENGL && gpu_rnd) ? gpu_rnd
                        : (cpu_rnd ? cpu_rnd : gpu_rnd);
    fprintf(stderr, "Renderer: %s (TAB to toggle)\n", rt_renderer_name(active));

    scene *scn;
    scene_camera *cam;
    build_scene(&scn, &cam);
    rt_scene_build_accel(scn);

    int preset = PRESET_DEFAULT;
    int render_w = PRESETS[preset].w;
    int render_h = PRESETS[preset].h;
    rt_viewport viewport = { render_w, render_h, FOV };
    uint32_t *pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
    uint32_t *gb_obj = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
    float *gb_depth = calloc((size_t)(render_w * render_h), sizeof(float));
    float *gb_normal = calloc((size_t)(3 * render_w * render_h), sizeof(float));

    fill_sky(pixels, render_w, render_h);

    int postfx_enabled = 1;
    postfx_bloom bloom_cfg = {
        .enabled    = 1,
        .threshold  = 0.60f,
        .knee       = 0.20f,
        .intensity  = 1.8f,
        .radius     = 10,
        .iterations = 2,
    };
    postfx_bloom_ctx *bloom = postfx_bloom_create(render_w, render_h);
    postfx_fog fog_cfg = {
        .enabled    = 1,
        .color      = {210, 205, 190},
        .start      = 5.0f,
        .end        = 50.0f,
        .max_strength = 0.6f,
        .skip_kinds_mask = 0,
    };
    postfx_vignette vignette_cfg = { .enabled = 1, .intensity = 0.30f, .softness = 0.55f };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &display_fbo);

    vector cam_pos = {-5.0f, 3.0f, -16.0f};
    float cam_yaw = atan2f(0.25f, 1.0f);
    float cam_pitch = -0.15f;

    int running = 1;
    Uint32 start_ticks = SDL_GetTicks();
    Uint32 frame_last = start_ticks;
    Uint32 fps_last = start_ticks;
    int fps_frames = 0;
    Uint32 render_ms_accum = 0;
    Uint32 postfx_ms_accum = 0;
    char title_buf[200];

    print_help();

    while (running) {
        Uint32 frame_now = SDL_GetTicks();
        float dt = (frame_now - frame_last) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        frame_last = frame_now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = 0;
                if (k == SDLK_TAB) {
                    if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                    else if (active == gpu_rnd && cpu_rnd) active = cpu_rnd;
                    fprintf(stderr, "Renderer: %s\n", rt_renderer_name(active));
                }
                if (k == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window,
                        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_GetWindowSize(window, &window_w, &window_h);
                }
                if (k == SDLK_p) {
                    postfx_enabled = !postfx_enabled;
                    fprintf(stderr, "PostFX: %s\n", postfx_enabled ? "on" : "off");
                }
                if (k == SDLK_b) {
                    bloom_cfg.enabled = !bloom_cfg.enabled;
                    fprintf(stderr, "Bloom: %s\n", bloom_cfg.enabled ? "on" : "off");
                }
                if (k == SDLK_f) {
                    fog_cfg.enabled = !fog_cfg.enabled;
                    fprintf(stderr, "Fog: %s\n", fog_cfg.enabled ? "on" : "off");
                }
                if (k == SDLK_v) {
                    vignette_cfg.enabled = !vignette_cfg.enabled;
                    fprintf(stderr, "Vignette: %s\n", vignette_cfg.enabled ? "on" : "off");
                }
                if (k == SDLK_h) print_help();
                if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
                    bloom_cfg.intensity *= 0.8f;
                    if (bloom_cfg.intensity < 0.05f) bloom_cfg.intensity = 0.05f;
                    fprintf(stderr, "Bloom intensity: %.2f\n", bloom_cfg.intensity);
                }
                if (k == SDLK_EQUALS || k == SDLK_KP_PLUS) {
                    bloom_cfg.intensity *= 1.25f;
                    if (bloom_cfg.intensity > 8.0f) bloom_cfg.intensity = 8.0f;
                    fprintf(stderr, "Bloom intensity: %.2f\n", bloom_cfg.intensity);
                }
                if (k == SDLK_LEFTBRACKET) {
                    if (bloom_cfg.radius > 1) bloom_cfg.radius--;
                    fprintf(stderr, "Bloom radius: %d\n", bloom_cfg.radius);
                }
                if (k == SDLK_RIGHTBRACKET) {
                    if (bloom_cfg.radius < 16) bloom_cfg.radius++;
                    fprintf(stderr, "Bloom radius: %d\n", bloom_cfg.radius);
                }
                if (k >= SDLK_1 && k <= SDLK_4) {
                    int idx = k - SDLK_1;
                    if (idx < PRESET_COUNT) {
                        preset = idx;
                        render_w = PRESETS[preset].w;
                        render_h = PRESETS[preset].h;
                        free(pixels);
                        free(gb_obj);
                        free(gb_depth);
                        free(gb_normal);
                        pixels = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
                        gb_obj = calloc((size_t)(render_w * render_h), sizeof(uint32_t));
                        gb_depth = calloc((size_t)(render_w * render_h), sizeof(float));
                        gb_normal = calloc((size_t)(3 * render_w * render_h), sizeof(float));
                        fill_sky(pixels, render_w, render_h);
                        viewport = (rt_viewport){ render_w, render_h, FOV };
                        glBindTexture(GL_TEXTURE_2D, display_tex);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                                     render_w, render_h, 0,
                                     GL_BGRA, GL_UNSIGNED_BYTE, NULL);
                        postfx_bloom_destroy(bloom);
                        bloom = postfx_bloom_create(render_w, render_h);
                        fprintf(stderr, "Preset: %s (%dx%d)\n",
                                PRESETS[preset].name, render_w, render_h);
                    }
                }
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_LEFT])  cam_yaw   -= LOOK_SPEED * dt;
        if (keys[SDL_SCANCODE_RIGHT]) cam_yaw   += LOOK_SPEED * dt;
        if (keys[SDL_SCANCODE_UP])    cam_pitch += LOOK_SPEED * dt;
        if (keys[SDL_SCANCODE_DOWN])  cam_pitch -= LOOK_SPEED * dt;
        if (cam_pitch >  PITCH_LIMIT) cam_pitch =  PITCH_LIMIT;
        if (cam_pitch < -PITCH_LIMIT) cam_pitch = -PITCH_LIMIT;

        vector fwd   = { sinf(cam_yaw), 0.0f, cosf(cam_yaw) };
        vector right = { cosf(cam_yaw), 0.0f, -sinf(cam_yaw) };
        float v = WALK_SPEED * dt;
        if (keys[SDL_SCANCODE_W]) cam_pos = vector_add(cam_pos, vector_scale(fwd,    v));
        if (keys[SDL_SCANCODE_S]) cam_pos = vector_add(cam_pos, vector_scale(fwd,   -v));
        if (keys[SDL_SCANCODE_D]) cam_pos = vector_add(cam_pos, vector_scale(right,  v));
        if (keys[SDL_SCANCODE_A]) cam_pos = vector_add(cam_pos, vector_scale(right, -v));
        if (keys[SDL_SCANCODE_SPACE])  cam_pos.y += v;
        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) cam_pos.y -= v;

        float margin = 30.0f;
        if (cam_pos.x < -margin) cam_pos.x = -margin;
        if (cam_pos.x >  margin) cam_pos.x =  margin;
        if (cam_pos.z < -margin) cam_pos.z = -margin;
        if (cam_pos.z >  margin) cam_pos.z =  margin;
        if (cam_pos.y < -1.0f) cam_pos.y = -1.0f;
        if (cam_pos.y > 35.0f) cam_pos.y = 35.0f;

        vector cam_dir = cam_dir_from_yaw_pitch(cam_yaw, cam_pitch);
        scene_camera_place(cam, cam_pos, cam_dir);

        fill_sky(pixels, render_w, render_h);

        int need_gbuf = postfx_enabled && fog_cfg.enabled;
        Uint32 r_start = SDL_GetTicks();
        rt_renderer_render(active, scn, cam, &viewport, pixels,
            need_gbuf ? &(rt_gbuffer){gb_obj, gb_depth, gb_normal} : NULL);
        Uint32 r_done = SDL_GetTicks();
        render_ms_accum += r_done - r_start;

        if (postfx_enabled) {
            postfx_bloom_apply(bloom, pixels, render_w, render_h, &bloom_cfg);
            if (need_gbuf)
                postfx_fog_apply(pixels, &(postfx_gbuffer){gb_obj, gb_depth, gb_normal},
                                 render_w, render_h, &fog_cfg);
            postfx_vignette_apply(pixels, render_w, render_h, &vignette_cfg);
        }
        postfx_ms_accum += SDL_GetTicks() - r_done;

        display_pixels(display_tex, display_fbo, pixels,
                       render_w, render_h, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        Uint32 now = SDL_GetTicks();
        if (now - fps_last >= 1000) {
            float avg_render = fps_frames ? (float)render_ms_accum / (float)fps_frames : 0.0f;
            float avg_postfx = fps_frames ? (float)postfx_ms_accum  / (float)fps_frames : 0.0f;
            snprintf(title_buf, sizeof(title_buf),
                     "Alpine Lake - %s %s  fx=%s bloom=%s fog=%s vig=%s  %d FPS (rt=%.1f fx=%.1f ms)",
                     rt_renderer_name(active), PRESETS[preset].name,
                     postfx_enabled ? "on" : "off",
                     bloom_cfg.enabled ? "on" : "off",
                     fog_cfg.enabled ? "on" : "off",
                     vignette_cfg.enabled ? "on" : "off",
                     fps_frames, avg_render, avg_postfx);
            SDL_SetWindowTitle(window, title_buf);
            fprintf(stderr, "[%s %s] %d FPS, rt=%.1fms fx=%.1fms\n",
                    rt_renderer_name(active), PRESETS[preset].name,
                    fps_frames, avg_render, avg_postfx);
            fps_frames = 0;
            render_ms_accum = 0;
            postfx_ms_accum = 0;
            fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    postfx_bloom_destroy(bloom);
    free(pixels);
    free(gb_obj);
    free(gb_depth);
    free(gb_normal);
    scene_camera_destroy(cam);
    scene_destroy(scn);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
