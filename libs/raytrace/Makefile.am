noinst_LTLIBRARIES = libraytrace.la
libraytrace_la_SOURCES = scene.c camera.c \
                         cpu/renderer.c cpu/render_chunk.c \
                         cpu/sphere.c cpu/plane.c cpu/disc.c \
                         cpu/cylinder.c cpu/triangle.c cpu/box.c \
                         cpu/sprite.c cpu/heightfield.c
noinst_HEADERS = viewport.h renderer.h rt_color.h scene.h camera.h \
                 sphere.h plane.h disc.h cylinder.h triangle.h box.h sprite.h \
                 heightfield.h cpu/render_chunk.h
libraytrace_la_CPPFLAGS = -I$(top_srcdir)/libs/math \
                          -I$(top_srcdir)/libs/thread \
                          -I$(top_srcdir)/libs/raytrace
libraytrace_la_LIBADD = -lm
