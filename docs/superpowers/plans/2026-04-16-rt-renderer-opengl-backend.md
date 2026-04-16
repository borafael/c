# RT Renderer OpenGL Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an OpenGL compute shader backend to the raytracer that runs alongside the CPU backend. The same scene renders identically on both backends; the primary difference is performance (GPU should be 10-100x faster). An A/B comparison harness in rtdemo allows side-by-side validation.

**Prerequisites:**
- The vtable refactor from `2026-04-16-rt-renderer-vtable-refactor.md` must be complete (it is — merged to `refactor/rt-renderer-vtable`).
- OpenGL 4.3+ capable GPU and drivers (for compute shaders).
- SDL2 with OpenGL support (standard — no extra flags needed).

**Architecture:**

The OpenGL backend lives in `libs/raytrace/opengl/` parallel to `libs/raytrace/cpu/`. It implements the same vtable contract:

```
rt_opengl_renderer_create() → rt_renderer * with:
    destroy_fn → opengl_destroy (releases GL resources)
    render_fn  → opengl_render (dispatch compute shader, readback pixels)
    name_fn    → opengl_name (returns "OpenGL")
```

**Rendering pipeline:**

1. App creates SDL window with `SDL_WINDOW_OPENGL` flag.
2. App creates SDL GL context via `SDL_GL_CreateContext()` and makes it current.
3. App calls `rt_renderer_create(RT_BACKEND_OPENGL)` — the renderer assumes a valid GL context exists.
4. At render time (`rt_renderer_render`):
   - Upload scene data (spheres, lights) to SSBOs.
   - Bind output texture (RGBA8, viewport dimensions).
   - Dispatch compute shader (one invocation per pixel).
   - `glMemoryBarrier` + `glReadPixels` to copy result into caller's `uint32_t *pixels`.
5. App uploads pixels to SDL texture and presents (same as CPU path).

**Why readback instead of direct display?**

The current API contract is `rt_renderer_render(..., uint32_t *pixels)` — the renderer writes to a CPU buffer. Changing this to "render to GPU texture" would require API changes and break the CPU backend's contract. Readback is slower but preserves API compatibility. Optimization (zero-copy display via GL texture) can be a follow-up.

**Shader strategy:**

- Single compute shader handles ray generation, intersection, and shading.
- Shader source is embedded as a C string constant (no external file loading).
- Scene data (spheres, lights, camera) passed via SSBOs and uniforms.
- Output is an `image2D` (rgba8) bound to a GL texture.

**Primitive support:**

Initial implementation supports **spheres only** (matching nbody). All 8 primitives are added for full CPU parity:

| Primitive | Complexity | Notes |
|---|---|---|
| sphere | Simple | Analytic ray-sphere intersection |
| plane | Simple | Analytic ray-plane intersection |
| disc | Simple | Plane + radius check |
| cylinder | Medium | Quadratic intersection + end caps |
| triangle | Medium | Möller–Trumbore algorithm |
| box | Medium | Slab method (AABB) |
| sprite | Complex | Billboard quad, texture upload, multi-frame selection, texture sampling |
| heightfield | Complex | Grid data upload (heights, colors, normals), DDA ray marching |

Sprite and heightfield require uploading large data arrays to GPU and more sophisticated shader logic.

**Tech Stack:** C (C11), GLSL 4.30, OpenGL 4.3+ (compute shaders), SDL2 (GL context), GNU Autotools.

**Working directory:** `/home/rafa/claude/c-rt-opengl-backend`, branch `feature/opengl-backend` based on `refactor/rt-renderer-vtable`.

**Before starting:**
1. Verify the vtable refactor branch builds and runs: `cd /home/rafa/claude/c-rt-renderer-vtable && make && ./apps/rtdemo/rtdemo`
2. Verify OpenGL 4.3+ is available: `glxinfo | grep "OpenGL version"` (should show 4.3 or higher).
3. Create the worktree from the vtable branch.

---

## File Structure

**Created:**
- `libs/raytrace/opengl/` — new subdirectory for OpenGL backend
- `libs/raytrace/opengl/renderer.c` — OpenGL renderer implementation
- `libs/raytrace/opengl/shader.h` — shader compilation helpers (static inline or small .c)
- `libs/raytrace/opengl/raytrace.glsl` — compute shader source (embedded in .c as string, but also kept as .glsl for syntax highlighting/editing)

**Modified:**
- `configure.ac` — add OpenGL detection, `RT_HAVE_OPENGL_BACKEND`, `BUILD_OPENGL_BACKEND`
- `libs/raytrace/Makefile.am` — add opengl/ sources conditionally
- `libs/raytrace/renderer.h` — add `RT_BACKEND_OPENGL` to enum
- `libs/raytrace/renderer.c` — add case arm for OpenGL in factory
- `apps/rtdemo/main.c` — switch to SDL GL context, add A/B comparison mode

**Unchanged:**
- All CPU backend files (`cpu/*.c`)
- Scene/camera/primitive types
- nbody, battleforge (they continue using CPU backend)

---

## Tasks

### Task 1: Build system plumbing for OpenGL

**Rationale:** Before writing any OpenGL code, establish the configure-time detection and conditional compilation scaffolding. This follows the pattern from Task 4 of the vtable refactor.

**Files:**
- Modify: `configure.ac`
- Modify: `libs/raytrace/Makefile.am`
- Modify: `libs/raytrace/renderer.h`
- Modify: `libs/raytrace/renderer.c`
- Create: `libs/raytrace/opengl/` (empty directory)
- Create: `libs/raytrace/opengl/renderer.c` (stub)

- [ ] **Step 1: Update `configure.ac`**

Add OpenGL detection after the CPU backend block:

```m4
# OpenGL backend — requires GL 4.3+ for compute shaders.
# Use pkg-config to find GL, or fall back to -lGL.
AC_ARG_ENABLE([opengl],
    AS_HELP_STRING([--enable-opengl], [Enable OpenGL compute backend (default: auto)]),
    [enable_opengl=$enableval],
    [enable_opengl=auto])

have_opengl=no
if test "x$enable_opengl" != "xno"; then
    PKG_CHECK_MODULES([GL], [gl], [have_opengl=yes], [
        # Fallback: check for -lGL directly
        AC_CHECK_LIB([GL], [glCreateShader], [have_opengl=yes; GL_LIBS="-lGL"])
    ])
    if test "x$have_opengl" = "xno" && test "x$enable_opengl" = "xyes"; then
        AC_MSG_ERROR([OpenGL requested but not found])
    fi
fi

if test "x$have_opengl" = "xyes"; then
    AC_DEFINE([RT_HAVE_OPENGL_BACKEND], [1], [OpenGL compute backend is available])
fi
AM_CONDITIONAL([BUILD_OPENGL_BACKEND], [test "x$have_opengl" = "xyes"])
```

- [ ] **Step 2: Update `libs/raytrace/Makefile.am`**

Add the OpenGL conditional block after the CPU block:

```make
if BUILD_OPENGL_BACKEND
libraytrace_la_SOURCES += opengl/renderer.c
libraytrace_la_CPPFLAGS += $(GL_CFLAGS)
libraytrace_la_LIBADD += $(GL_LIBS)
endif
```

- [ ] **Step 3: Update `libs/raytrace/renderer.h`**

Add to the `rt_backend` enum:

```c
typedef enum {
    RT_BACKEND_CPU = 0,
    RT_BACKEND_OPENGL = 1,
} rt_backend;
```

- [ ] **Step 4: Update `libs/raytrace/renderer.c`**

Add forward declaration and case arm:

```c
#ifdef RT_HAVE_OPENGL_BACKEND
rt_renderer *rt_opengl_renderer_create(void);
#endif

rt_renderer *rt_renderer_create(rt_backend type) {
    switch (type) {
#ifdef RT_HAVE_CPU_BACKEND
    case RT_BACKEND_CPU: return rt_cpu_renderer_create();
#endif
#ifdef RT_HAVE_OPENGL_BACKEND
    case RT_BACKEND_OPENGL: return rt_opengl_renderer_create();
#endif
    }
    return NULL;
}

int rt_renderer_available(rt_backend type) {
    switch (type) {
#ifdef RT_HAVE_CPU_BACKEND
    case RT_BACKEND_CPU: return 1;
#endif
#ifdef RT_HAVE_OPENGL_BACKEND
    case RT_BACKEND_OPENGL: return 1;
#endif
    }
    return 0;
}
```

- [ ] **Step 5: Create stub `libs/raytrace/opengl/renderer.c`**

```c
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef RT_HAVE_OPENGL_BACKEND

#include "renderer.h"
#include <stdlib.h>

typedef struct {
    int placeholder;  /* will hold GL objects later */
} opengl_backend_data;

static void opengl_destroy(rt_renderer *r) {
    free(r->backend_data);
    free(r);
}

static void opengl_render(rt_renderer *r,
                          const rt_scene *scene,
                          const rt_camera *camera,
                          const rt_viewport *viewport,
                          uint32_t *pixels) {
    (void)r; (void)scene; (void)camera; (void)viewport;
    /* Stub: fill with magenta to show it's running */
    for (int i = 0; i < viewport->width * viewport->height; i++) {
        pixels[i] = 0xFFFF00FF;  /* ARGB magenta */
    }
}

static const char *opengl_name(const rt_renderer *r) {
    (void)r;
    return "OpenGL";
}

rt_renderer *rt_opengl_renderer_create(void) {
    rt_renderer *r = calloc(1, sizeof(*r));
    if (!r) return NULL;

    opengl_backend_data *d = calloc(1, sizeof(*d));
    if (!d) { free(r); return NULL; }

    r->destroy_fn   = opengl_destroy;
    r->render_fn    = opengl_render;
    r->name_fn      = opengl_name;
    r->backend_data = d;
    return r;
}

#endif /* RT_HAVE_OPENGL_BACKEND */
```

This stub renders magenta, proving the backend is wired up before we add real GL code.

- [ ] **Step 6: Build and verify**

```bash
mkdir -p libs/raytrace/opengl
autoreconf -i && ./configure && make
./apps/rtdemo/rtdemo  # should still show CPU rendering
```

Check configure output shows OpenGL detection result.

- [ ] **Step 7: Test the stub backend**

Temporarily change rtdemo to use `RT_BACKEND_OPENGL`:

```c
rt_renderer *rnd = rt_renderer_create(RT_BACKEND_OPENGL);
```

Run rtdemo — should show solid magenta. Then revert to `RT_BACKEND_CPU`.

- [ ] **Step 8: Commit**

```bash
git add configure.ac libs/raytrace/Makefile.am libs/raytrace/renderer.h \
        libs/raytrace/renderer.c libs/raytrace/opengl/renderer.c
git commit -m "feat(raytrace): add OpenGL backend build plumbing + stub

Adds --enable-opengl configure flag, RT_BACKEND_OPENGL enum value, and
a stub opengl/renderer.c that renders magenta to prove the pipeline
works. Real GL code comes in subsequent commits.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

### Task 2: OpenGL context validation and shader infrastructure

**Rationale:** Before rendering, we need to verify a GL context exists and build helpers for shader compilation. This task adds the GL initialization checks and a reusable `compile_shader` / `link_program` infrastructure.

**Files:**
- Modify: `libs/raytrace/opengl/renderer.c`

- [ ] **Step 1: Add GL headers and context check**

```c
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <string.h>

/* Check GL context is valid and version >= 4.3 */
static int check_gl_context(void) {
    const char *version = (const char *)glGetString(GL_VERSION);
    if (!version) {
        fprintf(stderr, "rt_opengl: No GL context current\n");
        return 0;
    }
    int major = 0, minor = 0;
    sscanf(version, "%d.%d", &major, &minor);
    if (major < 4 || (major == 4 && minor < 3)) {
        fprintf(stderr, "rt_opengl: Requires GL 4.3+, got %d.%d\n", major, minor);
        return 0;
    }
    return 1;
}
```

- [ ] **Step 2: Add shader compilation helpers**

```c
static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "rt_opengl: Shader compile error:\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint create_compute_program(const char *source) {
    GLuint shader = compile_shader(GL_COMPUTE_SHADER, source);
    if (!shader) return 0;

    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);  /* attached, can delete */

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "rt_opengl: Program link error:\n%s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}
```

- [ ] **Step 3: Add minimal compute shader source**

```c
static const char *RAYTRACE_SHADER_SOURCE =
    "#version 430\n"
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "layout(rgba8, binding = 0) uniform writeonly image2D outputImage;\n"
    "\n"
    "void main() {\n"
    "    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);\n"
    "    ivec2 size = imageSize(outputImage);\n"
    "    if (pixel.x >= size.x || pixel.y >= size.y) return;\n"
    "\n"
    "    /* Gradient for now — proves the shader runs */\n"
    "    vec4 color = vec4(float(pixel.x) / float(size.x),\n"
    "                      float(pixel.y) / float(size.y),\n"
    "                      0.5, 1.0);\n"
    "    imageStore(outputImage, pixel, color);\n"
    "}\n";
```

- [ ] **Step 4: Update `opengl_backend_data` and create**

```c
typedef struct {
    GLuint program;
    GLuint output_texture;
    int tex_width, tex_height;
} opengl_backend_data;

rt_renderer *rt_opengl_renderer_create(void) {
    if (!check_gl_context()) return NULL;

    rt_renderer *r = calloc(1, sizeof(*r));
    if (!r) return NULL;

    opengl_backend_data *d = calloc(1, sizeof(*d));
    if (!d) { free(r); return NULL; }

    d->program = create_compute_program(RAYTRACE_SHADER_SOURCE);
    if (!d->program) {
        free(d); free(r);
        return NULL;
    }

    r->destroy_fn   = opengl_destroy;
    r->render_fn    = opengl_render;
    r->name_fn      = opengl_name;
    r->backend_data = d;
    return r;
}
```

- [ ] **Step 5: Update destroy to clean up GL objects**

```c
static void opengl_destroy(rt_renderer *r) {
    opengl_backend_data *d = r->backend_data;
    if (d->program) glDeleteProgram(d->program);
    if (d->output_texture) glDeleteTextures(1, &d->output_texture);
    free(d);
    free(r);
}
```

- [ ] **Step 6: Update render to dispatch shader and readback**

```c
static void ensure_texture(opengl_backend_data *d, int w, int h) {
    if (d->output_texture && d->tex_width == w && d->tex_height == h)
        return;
    if (d->output_texture) glDeleteTextures(1, &d->output_texture);
    
    glGenTextures(1, &d->output_texture);
    glBindTexture(GL_TEXTURE_2D, d->output_texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    d->tex_width = w;
    d->tex_height = h;
}

static void opengl_render(rt_renderer *r,
                          const rt_scene *scene,
                          const rt_camera *camera,
                          const rt_viewport *viewport,
                          uint32_t *pixels) {
    opengl_backend_data *d = r->backend_data;
    int w = viewport->width, h = viewport->height;
    (void)scene; (void)camera;  /* not used yet */

    ensure_texture(d, w, h);

    glUseProgram(d->program);
    glBindImageTexture(0, d->output_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    /* Dispatch: ceil(w/16) x ceil(h/16) workgroups */
    glDispatchCompute((w + 15) / 16, (h + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    /* Readback */
    glBindTexture(GL_TEXTURE_2D, d->output_texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
}
```

Note: `GL_BGRA` + `GL_UNSIGNED_BYTE` gives us ARGB layout matching the pixel buffer format.

- [ ] **Step 7: Build and test**

Temporarily switch rtdemo to use `RT_BACKEND_OPENGL`. Should show a gradient (red-green, proving shader runs). Then revert.

- [ ] **Step 8: Commit**

```bash
git commit -am "feat(raytrace/opengl): add shader infrastructure + gradient test

GL context validation, shader compile/link helpers, minimal compute
shader that renders a gradient to prove the pipeline works. Scene
data and actual raytracing come next.

Co-Authored-By: Claude Code (Claude Opus 4.6) <noreply@anthropic.com>"
```

---

### Task 3: Scene upload and camera uniforms

**Rationale:** The compute shader needs access to scene data (spheres, lights) and camera parameters. This task adds SSBO upload for primitives and uniforms for camera/viewport.

**Files:**
- Modify: `libs/raytrace/opengl/renderer.c`

- [ ] **Step 1: Define GPU-side structures**

These must match the GLSL layout:

```c
/* Must match GLSL layout exactly (std430) */
typedef struct {
    float center[4];   /* xyz + padding */
    float color[4];    /* rgb + radius */
} gpu_sphere;

typedef struct {
    float direction[4];  /* xyz + padding */
    float color[4];      /* rgb + intensity */
} gpu_dir_light;

typedef struct {
    float position[4];   /* xyz + padding */
    float color[4];      /* rgb + intensity */
    float attenuation[4]; /* constant, linear, quadratic, padding */
} gpu_point_light;
```

- [ ] **Step 2: Add SSBOs to backend data**

```c
typedef struct {
    GLuint program;
    GLuint output_texture;
    int tex_width, tex_height;
    
    GLuint sphere_ssbo;
    GLuint dir_light_ssbo;
    GLuint point_light_ssbo;
} opengl_backend_data;
```

- [ ] **Step 3: Create SSBOs in renderer_create**

```c
glGenBuffers(1, &d->sphere_ssbo);
glGenBuffers(1, &d->dir_light_ssbo);
glGenBuffers(1, &d->point_light_ssbo);
```

- [ ] **Step 4: Add scene upload function**

```c
static void upload_scene(opengl_backend_data *d, const rt_scene *scene) {
    /* Spheres */
    int n_spheres = rt_scene_sphere_count(scene);
    gpu_sphere *spheres = malloc(n_spheres * sizeof(gpu_sphere));
    for (int i = 0; i < n_spheres; i++) {
        const rt_sphere *s = rt_scene_get_sphere(scene, i);
        spheres[i].center[0] = s->center.x;
        spheres[i].center[1] = s->center.y;
        spheres[i].center[2] = s->center.z;
        spheres[i].center[3] = 0;
        spheres[i].color[0] = s->r / 255.0f;
        spheres[i].color[1] = s->g / 255.0f;
        spheres[i].color[2] = s->b / 255.0f;
        spheres[i].color[3] = s->radius;
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, d->sphere_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, n_spheres * sizeof(gpu_sphere), spheres, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, d->sphere_ssbo);
    free(spheres);

    /* Similar for lights... */
}
```

- [ ] **Step 5: Add camera uniforms**

```c
static void upload_camera(opengl_backend_data *d, 
                          const rt_camera *camera,
                          const rt_viewport *viewport) {
    vector origin, forward, right, up;
    rt_camera_get_basis(camera, &origin, &forward, &right, &up);
    
    glUniform3f(glGetUniformLocation(d->program, "u_cam_origin"),
                origin.x, origin.y, origin.z);
    glUniform3f(glGetUniformLocation(d->program, "u_cam_forward"),
                forward.x, forward.y, forward.z);
    glUniform3f(glGetUniformLocation(d->program, "u_cam_right"),
                right.x, right.y, right.z);
    glUniform3f(glGetUniformLocation(d->program, "u_cam_up"),
                up.x, up.y, up.z);
    glUniform1f(glGetUniformLocation(d->program, "u_fov"), viewport->fov);
    glUniform1i(glGetUniformLocation(d->program, "u_sphere_count"), 
                rt_scene_sphere_count(scene));
}
```

- [ ] **Step 6: Call upload functions in render**

```c
static void opengl_render(...) {
    /* ... texture setup ... */
    
    glUseProgram(d->program);
    upload_scene(d, scene);
    upload_camera(d, camera, viewport);
    
    /* ... dispatch ... */
}
```

- [ ] **Step 7: Commit**

---

### Task 4: Compute shader raytracer

**Rationale:** Replace the gradient shader with actual ray-sphere intersection and Lambertian shading, matching the CPU implementation.

**Files:**
- Modify: `libs/raytrace/opengl/renderer.c` (shader source)

- [ ] **Step 1: Update shader source**

```glsl
#version 430
layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba8, binding = 0) uniform writeonly image2D outputImage;

/* Camera uniforms */
uniform vec3 u_cam_origin;
uniform vec3 u_cam_forward;
uniform vec3 u_cam_right;
uniform vec3 u_cam_up;
uniform float u_fov;
uniform int u_sphere_count;

/* Sphere SSBO */
struct Sphere {
    vec4 center;  /* xyz = center, w = unused */
    vec4 color;   /* rgb = color, a = radius */
};
layout(std430, binding = 1) readonly buffer SphereBuffer {
    Sphere spheres[];
};

/* Ray-sphere intersection */
float intersect_sphere(vec3 ro, vec3 rd, Sphere s) {
    vec3 oc = ro - s.center.xyz;
    float r = s.color.a;
    float b = 2.0 * dot(oc, rd);
    float c = dot(oc, oc) - r * r;
    float disc = b * b - 4.0 * c;
    if (disc < 0.0) return -1.0;
    float t = (-b - sqrt(disc)) / 2.0;
    return t > 0.0 ? t : -1.0;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outputImage);
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    /* Ray generation */
    float aspect = float(size.x) / float(size.y);
    float fov_factor = float(size.y) / (2.0 * tan(u_fov / 2.0));
    float sx = (float(pixel.x) - float(size.x) / 2.0) / fov_factor;
    float sy = -(float(pixel.y) - float(size.y) / 2.0) / fov_factor;
    
    vec3 ro = u_cam_origin;
    vec3 rd = normalize(u_cam_forward + sx * u_cam_right + sy * u_cam_up);

    /* Find closest hit */
    float closest_t = 1e30;
    int closest_idx = -1;
    for (int i = 0; i < u_sphere_count; i++) {
        float t = intersect_sphere(ro, rd, spheres[i]);
        if (t > 0.0 && t < closest_t) {
            closest_t = t;
            closest_idx = i;
        }
    }

    vec4 color;
    if (closest_idx < 0) {
        color = vec4(0.0, 0.0, 0.0, 1.0);  /* background */
    } else {
        /* Shading */
        Sphere s = spheres[closest_idx];
        vec3 hit = ro + closest_t * rd;
        vec3 normal = normalize(hit - s.center.xyz);
        vec3 light_dir = normalize(vec3(1.0, 1.0, -1.0));
        float diff = max(0.0, dot(normal, light_dir));
        float shade = 0.15 + 0.85 * diff;  /* ambient + diffuse */
        color = vec4(s.color.rgb * shade, 1.0);
    }
    
    imageStore(outputImage, pixel, color);
}
```

- [ ] **Step 2: Verify scene accessor functions exist**

The shader needs `rt_scene_sphere_count()` and `rt_scene_get_sphere()`. Check `libs/raytrace/scene.h` — if they don't exist, add them or iterate differently.

- [ ] **Step 3: Test with nbody**

nbody uses spheres only. Temporarily switch it to `RT_BACKEND_OPENGL`, verify spheres render correctly (same positions, colors, shading as CPU).

- [ ] **Step 4: Commit**

---

### Task 5: Migrate rtdemo to SDL GL context + A/B harness

**Rationale:** rtdemo currently uses `SDL_Renderer` which abstracts away the GL context. To use the OpenGL backend, rtdemo needs to create an explicit GL context. This task also adds an A/B comparison mode.

**Files:**
- Modify: `apps/rtdemo/main.c`

Key changes:
1. Create window with `SDL_WINDOW_OPENGL` flag.
2. Create GL context via `SDL_GL_CreateContext()`.
3. Create both CPU and OpenGL renderers.
4. Render both every frame to separate buffers.
5. Display side-by-side or allow toggle with a key.
6. Show FPS for each renderer.

- [ ] **Step 1: Add GL context setup**

Replace `SDL_CreateRenderer` with:

```c
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

SDL_Window *window = SDL_CreateWindow("Raytrace Demo",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    WINDOW_W, WINDOW_H, SDL_WINDOW_OPENGL);
SDL_GLContext gl_context = SDL_GL_CreateContext(window);
```

- [ ] **Step 2: Create both renderers**

```c
rt_renderer *cpu_rnd = rt_renderer_create(RT_BACKEND_CPU);
rt_renderer *gpu_rnd = rt_renderer_available(RT_BACKEND_OPENGL)
                     ? rt_renderer_create(RT_BACKEND_OPENGL)
                     : NULL;
```

- [ ] **Step 3: Render both, measure separately**

```c
Uint32 cpu_start = SDL_GetTicks();
rt_renderer_render(cpu_rnd, scene, camera, &viewport, cpu_pixels);
Uint32 cpu_time = SDL_GetTicks() - cpu_start;

Uint32 gpu_start = SDL_GetTicks();
if (gpu_rnd) {
    rt_renderer_render(gpu_rnd, scene, camera, &viewport, gpu_pixels);
}
Uint32 gpu_time = SDL_GetTicks() - gpu_start;
```

- [ ] **Step 4: Display side-by-side**

Split the window: left half shows CPU, right half shows GPU. Or use a key to toggle.

- [ ] **Step 5: Commit**

---

### Task 6: Add remaining primitives

**Rationale:** rtdemo uses planes, discs, and triangles in addition to spheres. Add these to the shader for full scene parity.

**Files:**
- Modify: `libs/raytrace/opengl/renderer.c`

For each primitive:
1. Add GPU struct matching the rt_* type.
2. Add SSBO upload in `upload_scene`.
3. Add intersection function in shader.
4. Add iteration loop in shader's main().

Primitives to add (in order of complexity):

**Simple (analytic intersection):**
- [ ] Plane
- [ ] Disc

**Medium (quadratic or algorithmic):**
- [ ] Cylinder
- [ ] Triangle
- [ ] Box

**Complex (data upload + advanced shader logic):**
- [ ] Sprite — requires texture upload (SSBO or texture array for frames), billboard orientation from camera, frame selection, texture sampling in shader
- [ ] Heightfield — requires uploading heights/colors/normals arrays, DDA grid traversal in shader, bilinear interpolation for smooth normals

---

### Task 7: Verification and documentation

- [ ] **Visual parity check**: CPU and OpenGL render identically (pixel-perfect or near-identical due to float precision).
- [ ] **Performance check**: OpenGL is significantly faster (expect 10-100x).
- [ ] **Update GPU raytrace idea seed** with lessons learned.
- [ ] **Final commit** cleaning up any debug code.

---

### Task 8: OpenGL-specific optimizations

**Rationale:** The initial implementation prioritizes correctness and CPU parity. This task applies GPU-specific optimizations that go beyond a direct port but don't require restructuring to triangles (which would be needed for Vulkan RT). These optimizations leverage OpenGL's texture hardware and caching.

**Benchmark first:** Before optimizing, measure baseline FPS on a representative scene (rtdemo with all primitive types). Record CPU time, GPU time, and bottlenecks (upload, dispatch, readback).

#### Heightfield optimizations

- [ ] **Convert height array to `sampler2D`**
  - Replace SSBO float array with a single-channel float texture (`GL_R32F`)
  - Use `texture()` instead of manual array indexing
  - Benefit: hardware bilinear interpolation, texture cache

- [ ] **Add mipmap hierarchy for hierarchical marching**
  - Generate mipmaps of the height texture
  - Each mip level stores max height in that region
  - Ray marches coarse-to-fine, skipping empty space
  - Benefit: 10-50x faster for grazing/shallow rays

- [ ] **Pre-compute normal map**
  - Store normals as `sampler2D` (RGB = XYZ)
  - One texture sample instead of 4-point gradient calculation
  - Benefit: fewer ALU ops, better cache

#### Sprite optimizations

- [ ] **Convert frames to `sampler2DArray`**
  - Each sprite's frames become layers in a texture array
  - Use `texture(sampler, vec3(uv, frame_index))`
  - Benefit: hardware filtering, single binding for all frames

- [ ] **Pre-compute billboard quads on CPU**
  - Instead of computing billboard orientation per-ray in shader
  - Upload pre-oriented quad corners as part of sprite data
  - Intersection becomes simple ray-quad test
  - Benefit: less per-ray math, more coherent memory access

#### General optimizations

- [ ] **Persistent SSBOs with dirty tracking**
  - Don't re-upload scene data every frame if unchanged
  - Add `scene_version` counter, compare before upload
  - Benefit: reduces CPU→GPU bandwidth

- [ ] **Workgroup shared memory for scene data**
  - Cache frequently-accessed primitives in shared memory
  - Useful if many rays hit the same objects
  - Benefit: reduce global memory traffic

- [ ] **Simple spatial acceleration (optional)**
  - Build a uniform grid or BVH on CPU
  - Upload to GPU, use for early-out
  - Benefit: skip distant primitives entirely
  - Note: only worth it for scenes with many primitives

#### Verification

- [ ] **Benchmark after each optimization** — confirm speedup, no visual regression
- [ ] **Document results** — which optimizations helped most, on what hardware

---

## Post-plan validation

```bash
# Check configure detects OpenGL
./configure | grep -i opengl

# Build with OpenGL enabled
make

# Run A/B comparison
./apps/rtdemo/rtdemo
# Verify: both sides render the same scene, GPU side shows higher FPS

# Run nbody with GPU
# (modify temporarily or add command-line flag)
```

---

## Rollback plan

```bash
git checkout refactor/rt-renderer-vtable
git branch -D feature/opengl-backend
```

The OpenGL backend is purely additive — removing it doesn't affect the CPU path.

---

## What's deliberately NOT in scope

1. **Zero-copy display.** Pixels are read back from GPU to CPU buffer. Direct GL texture display would require API changes.
2. **Point lights.** Only directional light for now (matches CPU's hard-coded light direction).
3. **Shadows, reflections, transparency.** The CPU backend doesn't have these either.
4. **Async rendering / double buffering.** One frame at a time, synchronous.
5. **Vulkan, CUDA, or other backends.** OpenGL only.
6. **macOS support.** macOS caps OpenGL at 4.1 (no compute shaders). Use Metal there.
7. **Mobile / OpenGL ES.** Desktop GL 4.3+ only.
