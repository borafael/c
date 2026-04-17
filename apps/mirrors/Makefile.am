bin_PROGRAMS = mirrors
mirrors_SOURCES = main.c

mirrors_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/thread $(SDL2_CFLAGS) $(GL_CFLAGS)
mirrors_LDADD = $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/thread/libthread.la -lm -lpthread $(SDL2_LIBS) $(GL_LIBS)
