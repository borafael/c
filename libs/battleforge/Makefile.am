noinst_LTLIBRARIES = libbattleforge.la
libbattleforge_la_SOURCES = battleforge.c
libbattleforge_la_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/thread
libbattleforge_la_LIBADD = $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/thread/libthread.la -lm -lpthread
