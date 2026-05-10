bin_PROGRAMS = lowspec
lowspec_SOURCES = main.c

lowspec_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/scene -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/thread
lowspec_LDADD = $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/scene/libscene.la $(top_builddir)/libs/thread/libthread.la -lm -lpthread
