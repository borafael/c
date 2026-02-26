bin_PROGRAMS = nbody
nbody_SOURCES = main.c nbody.c render.c input.c

nbody_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/thread $(SDL2_CFLAGS)
nbody_LDADD = $(top_builddir)/libs/thread/libthread.la -lm $(SDL2_LIBS)
