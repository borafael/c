bin_PROGRAMS = portal_sphere
portal_sphere_SOURCES = main.c

portal_sphere_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/scene -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/thread $(SDL2_CFLAGS) $(GL_CFLAGS)
portal_sphere_LDADD = $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/scene/libscene.la $(top_builddir)/libs/thread/libthread.la -lm -lpthread $(SDL2_LIBS) $(GL_LIBS)
