#include "gl_compat.h"

#include <stdint.h>

#ifdef _WIN32
#include <stdio.h>

#define GL_COMPAT_DEFINE(type, name) type gl_compat_##name = 0;
GL_COMPAT_DISPLAY_FUNCS(GL_COMPAT_DEFINE)
GL_COMPAT_COMPUTE_FUNCS(GL_COMPAT_DEFINE)
#undef GL_COMPAT_DEFINE

static int g_has_compute = 0;

/* wglGetProcAddress can return small sentinels (1, 2, 3) or -1 instead of
 * NULL for entry points the driver lacks; treat those as missing too. */
static void *checked(gl_compat_loader_fn load, const char *name) {
    void *p = load(name);
    uintptr_t v = (uintptr_t)p;
    if (v <= 3 || v == (uintptr_t)-1) return NULL;
    return p;
}
#endif

void gl_compat_init(gl_compat_loader_fn load)
{
#ifdef _WIN32
#define GL_COMPAT_LOAD(type, name) gl_compat_##name = (type)checked(load, "gl" #name);
    GL_COMPAT_DISPLAY_FUNCS(GL_COMPAT_LOAD)
    GL_COMPAT_COMPUTE_FUNCS(GL_COMPAT_LOAD)
#undef GL_COMPAT_LOAD

    g_has_compute = 1;
#define GL_COMPAT_CHECK(type, name)                                          \
    if (!gl_compat_##name) {                                                 \
        if (g_has_compute)                                                   \
            fprintf(stderr, "gl_compat: driver lacks gl" #name               \
                            " (GL 4.3 compute backend unavailable)\n");      \
        g_has_compute = 0;                                                   \
    }
    GL_COMPAT_COMPUTE_FUNCS(GL_COMPAT_CHECK)
#undef GL_COMPAT_CHECK
#else
    (void)load;
#endif
}

int gl_compat_has_compute(void)
{
#ifdef _WIN32
    return g_has_compute;
#else
    return 1;
#endif
}
