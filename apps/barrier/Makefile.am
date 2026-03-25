bin_PROGRAMS = barrier
barrier_SOURCES = main.c console.c
barrier_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/battleforge -I$(top_srcdir)/libs/thread -I$(top_srcdir)/libs/slice -I$(top_srcdir)/libs/ini $(SDL2_CFLAGS)
barrier_LDADD = $(top_builddir)/libs/battleforge/libbattleforge.la $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/thread/libthread.la $(top_builddir)/libs/slice/libslice.la $(top_builddir)/libs/ini/libini.la -lm -lpthread $(SDL2_LIBS)
