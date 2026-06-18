bin_PROGRAMS = room
room_SOURCES = main.c

room_CPPFLAGS = -I$(top_srcdir)/libs/math \
                -I$(top_srcdir)/libs/scene \
                -I$(top_srcdir)/libs/raytrace \
                -I$(top_srcdir)/libs/thread \
                -I$(top_srcdir)/libs/postfx \
                $(SDL2_CFLAGS) $(GL_CFLAGS)

room_LDADD = $(top_builddir)/libs/raytrace/libraytrace.la \
             $(top_builddir)/libs/scene/libscene.la \
             $(top_builddir)/libs/thread/libthread.la \
             $(top_builddir)/libs/postfx/libpostfx.la \
             -lm -lpthread $(SDL2_LIBS) $(GL_LIBS)
