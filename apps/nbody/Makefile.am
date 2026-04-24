bin_PROGRAMS = nbody
nbody_SOURCES = main.c nbody.c render.c input.c

nbody_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/scene -I$(top_srcdir)/libs/thread -I$(top_srcdir)/libs/physics -I$(top_srcdir)/libs/raytrace $(SDL2_CFLAGS) $(GL_CFLAGS)
nbody_LDADD = $(top_builddir)/libs/physics/libphysics.la $(top_builddir)/libs/thread/libthread.la $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/scene/libscene.la -lm $(SDL2_LIBS) $(GL_LIBS)
