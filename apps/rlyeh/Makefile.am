bin_PROGRAMS = rlyeh
rlyeh_SOURCES = main.c world.h world_rlyeh.c world_lighthouse.c world_lighthouse_top.c whisper.c whisper.h bench.c bench.h audio.c audio.h vendor/dr_mp3.h

rlyeh_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/scene -I$(top_srcdir)/libs/raytrace -I$(top_srcdir)/libs/postfx -I$(top_srcdir)/libs/thread $(SDL2_CFLAGS) $(GL_CFLAGS)
rlyeh_LDADD = $(top_builddir)/libs/raytrace/libraytrace.la $(top_builddir)/libs/scene/libscene.la $(top_builddir)/libs/postfx/libpostfx.la $(top_builddir)/libs/thread/libthread.la -lm -lpthread $(SDL2_LIBS) $(GL_LIBS)

EXTRA_DIST = assets/ambient.mp3
