noinst_LTLIBRARIES = libraytrace.la
libraytrace_la_SOURCES = raytrace.c scene.c camera.c sphere.c plane.c disc.c \
                         cylinder.c triangle.c box.c sprite.c heightfield.c
noinst_HEADERS = raytrace.h viewport.h rt_color.h scene.h camera.h sphere.h \
                 plane.h disc.h cylinder.h triangle.h box.h sprite.h heightfield.h
libraytrace_la_CPPFLAGS = -I$(top_srcdir)/libs/math
libraytrace_la_LIBADD = -lm
