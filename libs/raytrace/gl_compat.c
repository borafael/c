#include "gl_compat.h"

#ifdef _WIN32
PFNGLGENFRAMEBUFFERSPROC      gl_compat_GenFramebuffers      = 0;
PFNGLBINDFRAMEBUFFERPROC      gl_compat_BindFramebuffer      = 0;
PFNGLFRAMEBUFFERTEXTURE2DPROC gl_compat_FramebufferTexture2D = 0;
PFNGLBLITFRAMEBUFFERPROC      gl_compat_BlitFramebuffer      = 0;
PFNGLDELETEFRAMEBUFFERSPROC   gl_compat_DeleteFramebuffers   = 0;
#endif

void gl_compat_init(gl_compat_loader_fn load)
{
#ifdef _WIN32
    gl_compat_GenFramebuffers      = (PFNGLGENFRAMEBUFFERSPROC)      load("glGenFramebuffers");
    gl_compat_BindFramebuffer      = (PFNGLBINDFRAMEBUFFERPROC)      load("glBindFramebuffer");
    gl_compat_FramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC) load("glFramebufferTexture2D");
    gl_compat_BlitFramebuffer      = (PFNGLBLITFRAMEBUFFERPROC)      load("glBlitFramebuffer");
    gl_compat_DeleteFramebuffers   = (PFNGLDELETEFRAMEBUFFERSPROC)   load("glDeleteFramebuffers");
#else
    (void)load;
#endif
}
