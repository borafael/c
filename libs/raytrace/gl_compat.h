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
 * newer (FBOs, blits, shaders, compute, GL 3.x/4.x) must be loaded at
 * runtime via SDL_GL_GetProcAddress (or wgl). gl_compat_init() populates
 * function pointers that this header then aliases over the bare GL names,
 * so app code and the OpenGL compute backend keep calling glCreateShader
 * etc. verbatim.
 *
 * Usage from an app, after SDL_GL_CreateContext and BEFORE creating an
 * RT_BACKEND_OPENGL renderer:
 *
 *     #include "gl_compat.h"
 *     gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);
 *
 * The callback indirection keeps libraytrace free of an SDL2 link-time
 * dependency.
 */

#ifdef __APPLE__
/* macOS: core-profile GL (3.2+) comes from the framework header, which
 * already declares the FBO/blit entry points the apps use. */
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*gl_compat_loader_fn)(const char *name);

void gl_compat_init(gl_compat_loader_fn load);

/* 1 if every entry point the OpenGL compute backend needs is callable.
 * Always 1 off Windows (they're link-time symbols there). On Windows it
 * is 0 until gl_compat_init() ran against a context whose driver exposes
 * them all — the backend checks it and declines to start otherwise, so
 * apps fall back to the CPU renderer instead of calling a NULL pointer. */
int gl_compat_has_compute(void);

#ifdef _WIN32
/* Display path (FBO + blit), used by every app. */
#define GL_COMPAT_DISPLAY_FUNCS(X)                                      \
    X(PFNGLGENFRAMEBUFFERSPROC,       GenFramebuffers)                  \
    X(PFNGLBINDFRAMEBUFFERPROC,       BindFramebuffer)                  \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC,  FramebufferTexture2D)             \
    X(PFNGLBLITFRAMEBUFFERPROC,       BlitFramebuffer)                  \
    X(PFNGLDELETEFRAMEBUFFERSPROC,    DeleteFramebuffers)

/* OpenGL compute raytrace backend (libs/raytrace/opengl/renderer.c):
 * everything it calls beyond GL 1.1. */
#define GL_COMPAT_COMPUTE_FUNCS(X)                                      \
    X(PFNGLACTIVETEXTUREPROC,         ActiveTexture)                    \
    X(PFNGLATTACHSHADERPROC,          AttachShader)                     \
    X(PFNGLBINDBUFFERPROC,            BindBuffer)                       \
    X(PFNGLBINDBUFFERBASEPROC,        BindBufferBase)                   \
    X(PFNGLBINDIMAGETEXTUREPROC,      BindImageTexture)                 \
    X(PFNGLBUFFERDATAPROC,            BufferData)                       \
    X(PFNGLCOMPILESHADERPROC,         CompileShader)                    \
    X(PFNGLCREATEPROGRAMPROC,         CreateProgram)                    \
    X(PFNGLCREATESHADERPROC,          CreateShader)                     \
    X(PFNGLDELETEBUFFERSPROC,         DeleteBuffers)                    \
    X(PFNGLDELETEPROGRAMPROC,         DeleteProgram)                    \
    X(PFNGLDELETESHADERPROC,          DeleteShader)                     \
    X(PFNGLDISPATCHCOMPUTEPROC,       DispatchCompute)                  \
    X(PFNGLGENBUFFERSPROC,            GenBuffers)                       \
    X(PFNGLGETPROGRAMINFOLOGPROC,     GetProgramInfoLog)                \
    X(PFNGLGETPROGRAMIVPROC,          GetProgramiv)                     \
    X(PFNGLGETSHADERINFOLOGPROC,      GetShaderInfoLog)                 \
    X(PFNGLGETSHADERIVPROC,           GetShaderiv)                      \
    X(PFNGLGETUNIFORMLOCATIONPROC,    GetUniformLocation)               \
    X(PFNGLLINKPROGRAMPROC,           LinkProgram)                      \
    X(PFNGLMEMORYBARRIERPROC,         MemoryBarrier)                    \
    X(PFNGLSHADERSOURCEPROC,          ShaderSource)                     \
    X(PFNGLTEXSTORAGE2DPROC,          TexStorage2D)                     \
    X(PFNGLTEXSTORAGE3DPROC,          TexStorage3D)                     \
    X(PFNGLTEXSUBIMAGE3DPROC,         TexSubImage3D)                    \
    X(PFNGLUNIFORM1FPROC,             Uniform1f)                        \
    X(PFNGLUNIFORM1IPROC,             Uniform1i)                        \
    X(PFNGLUNIFORM3FPROC,             Uniform3f)                        \
    X(PFNGLUSEPROGRAMPROC,            UseProgram)

#define GL_COMPAT_DECLARE(type, name) extern type gl_compat_##name;
GL_COMPAT_DISPLAY_FUNCS(GL_COMPAT_DECLARE)
GL_COMPAT_COMPUTE_FUNCS(GL_COMPAT_DECLARE)
#undef GL_COMPAT_DECLARE

#define glGenFramebuffers      gl_compat_GenFramebuffers
#define glBindFramebuffer      gl_compat_BindFramebuffer
#define glFramebufferTexture2D gl_compat_FramebufferTexture2D
#define glBlitFramebuffer      gl_compat_BlitFramebuffer
#define glDeleteFramebuffers   gl_compat_DeleteFramebuffers

#define glActiveTexture        gl_compat_ActiveTexture
#define glAttachShader         gl_compat_AttachShader
#define glBindBuffer           gl_compat_BindBuffer
#define glBindBufferBase       gl_compat_BindBufferBase
#define glBindImageTexture     gl_compat_BindImageTexture
#define glBufferData           gl_compat_BufferData
#define glCompileShader        gl_compat_CompileShader
#define glCreateProgram        gl_compat_CreateProgram
#define glCreateShader         gl_compat_CreateShader
#define glDeleteBuffers        gl_compat_DeleteBuffers
#define glDeleteProgram        gl_compat_DeleteProgram
#define glDeleteShader         gl_compat_DeleteShader
#define glDispatchCompute      gl_compat_DispatchCompute
#define glGenBuffers           gl_compat_GenBuffers
#define glGetProgramInfoLog    gl_compat_GetProgramInfoLog
#define glGetProgramiv         gl_compat_GetProgramiv
#define glGetShaderInfoLog     gl_compat_GetShaderInfoLog
#define glGetShaderiv          gl_compat_GetShaderiv
#define glGetUniformLocation   gl_compat_GetUniformLocation
#define glLinkProgram          gl_compat_LinkProgram
#define glMemoryBarrier        gl_compat_MemoryBarrier
#define glShaderSource         gl_compat_ShaderSource
#define glTexStorage2D         gl_compat_TexStorage2D
#define glTexStorage3D         gl_compat_TexStorage3D
#define glTexSubImage3D        gl_compat_TexSubImage3D
#define glUniform1f            gl_compat_Uniform1f
#define glUniform1i            gl_compat_Uniform1i
#define glUniform3f            gl_compat_Uniform3f
#define glUseProgram           gl_compat_UseProgram
#endif

#ifdef __cplusplus
}
#endif

#endif
