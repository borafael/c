noinst_LTLIBRARIES = libraytrace.la

libraytrace_la_SOURCES = renderer.c scene_accel.c gl_compat.c

if BUILD_CPU_BACKEND
libraytrace_la_SOURCES += cpu/renderer.c cpu/render_chunk.c \
                          cpu/sphere.c cpu/plane.c cpu/disc.c \
                          cpu/cylinder.c cpu/triangle.c cpu/box.c \
                          cpu/sprite.c cpu/heightfield.c \
                          cpu/mesh.c cpu/bvh.c
endif

noinst_HEADERS = viewport.h renderer.h scene_accel.h gl_compat.h \
                 sphere.h plane.h disc.h cylinder.h triangle.h box.h \
                 sprite.h heightfield.h mesh.h \
                 cpu/render_chunk.h cpu/bvh.h
libraytrace_la_CPPFLAGS = -I$(top_srcdir)/libs/math \
                          -I$(top_srcdir)/libs/scene \
                          -I$(top_srcdir)/libs/thread \
                          -I$(top_srcdir)/libs/raytrace \
                          $(GL_CFLAGS)
libraytrace_la_LIBADD = $(top_builddir)/libs/scene/libscene.la -lm

if BUILD_OPENGL_BACKEND
libraytrace_la_SOURCES += opengl/renderer.c
libraytrace_la_CPPFLAGS += $(GL_CFLAGS)
libraytrace_la_LIBADD += $(GL_LIBS)
endif
