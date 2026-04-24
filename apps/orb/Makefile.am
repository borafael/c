bin_PROGRAMS = orb
orb_SOURCES = main.c

orb_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/scene -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/thread $(SDL2_CFLAGS) $(GL_CFLAGS)
orb_LDADD = $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/scene/libscene.la $(top_builddir)/libs/thread/libthread.la -lm -lpthread $(SDL2_LIBS) $(GL_LIBS)
