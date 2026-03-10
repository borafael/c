bin_PROGRAMS = rtdemo
rtdemo_SOURCES = main.c

rtdemo_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/raytrace $(SDL2_CFLAGS)
rtdemo_LDADD = $(top_builddir)/libs/raytrace/libraytrace.la -lm $(SDL2_LIBS)
