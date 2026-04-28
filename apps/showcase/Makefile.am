bin_PROGRAMS = showcase
showcase_SOURCES = main.c

showcase_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/scene -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/postfx -I$(top_srcdir)/libs/thread $(SDL2_CFLAGS) $(GL_CFLAGS)
showcase_LDADD = $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/scene/libscene.la $(top_builddir)/libs/postfx/libpostfx.la $(top_builddir)/libs/thread/libthread.la -lm -lpthread $(SDL2_LIBS) $(GL_LIBS)
