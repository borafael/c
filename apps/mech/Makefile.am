bin_PROGRAMS = mech
mech_SOURCES = main.c

mech_CPPFLAGS = -I$(top_srcdir)/libs/math \
                -I$(top_srcdir)/libs/scene \
                -I$(top_srcdir)/libs/raytrace \
                -I$(top_srcdir)/libs/thread \
                -I$(top_srcdir)/libs/ini \
                $(SDL2_CFLAGS) $(GL_CFLAGS)

mech_LDADD = $(top_builddir)/libs/raytrace/libraytrace.la \
             $(top_builddir)/libs/scene/libscene.la \
             $(top_builddir)/libs/thread/libthread.la \
             $(top_builddir)/libs/ini/libini.la \
             -lm -lpthread $(SDL2_LIBS) $(GL_LIBS)
