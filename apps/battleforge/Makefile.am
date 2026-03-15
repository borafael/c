bin_PROGRAMS = battleforge
battleforge_SOURCES = main.c
battleforge_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/battleforge -I$(top_srcdir)/libs/thread -I$(top_srcdir)/libs/slice $(SDL2_CFLAGS)
battleforge_LDADD = $(top_builddir)/libs/battleforge/libbattleforge.la $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/thread/libthread.la $(top_builddir)/libs/slice/libslice.la -lm -lpthread $(SDL2_LIBS)
