#ifndef RT_GL_COMPAT_H
#define RT_GL_COMPAT_H

/*
 * Tiny OpenGL function-pointer loader.
 *
 * On Linux (and any platform where GL_GLEXT_PROTOTYPES resolves), this
 * header is a no-op — apps call glGenFramebuffers etc. as normal symbols
 * resolved against libGL at link time.
 *
 * On Windows, opengl32.dll only exports GL 1.1 entry points. Anything
 * newer (FBOs, blits, GL 3.x+) must be loaded at runtime via
 * SDL_GL_GetProcAddress (or wgl). gl_compat_init() populates a small
 * set of function pointers that this header then aliases over the bare
 * GL names, so app code can keep calling glGenFramebuffers verbatim.
 *
 * Usage from an app, after SDL_GL_CreateContext:
 *
 *     #include "gl_compat.h"
 *     gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);
 *
 * The callback indirection keeps libraytrace free of an SDL2 link-time
 * dependency.
 */

#include <GL/gl.h>
#include <GL/glext.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*gl_compat_loader_fn)(const char *name);

void gl_compat_init(gl_compat_loader_fn load);

#ifdef _WIN32
extern PFNGLGENFRAMEBUFFERSPROC      gl_compat_GenFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC      gl_compat_BindFramebuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC gl_compat_FramebufferTexture2D;
extern PFNGLBLITFRAMEBUFFERPROC      gl_compat_BlitFramebuffer;
extern PFNGLDELETEFRAMEBUFFERSPROC   gl_compat_DeleteFramebuffers;

#define glGenFramebuffers      gl_compat_GenFramebuffers
#define glBindFramebuffer      gl_compat_BindFramebuffer
#define glFramebufferTexture2D gl_compat_FramebufferTexture2D
#define glBlitFramebuffer      gl_compat_BlitFramebuffer
#define glDeleteFramebuffers   gl_compat_DeleteFramebuffers
#endif

#ifdef __cplusplus
}
#endif

#endif
