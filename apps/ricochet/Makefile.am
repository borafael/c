bin_PROGRAMS = ricochet
ricochet_SOURCES = main.c \
                   app.c app.h \
                   game.c game.h game_internal.h \
                   enemy.c \
                   level.c level.h \
                   levels.c levels.h \
                   nav.c nav.h \
                   bot.c bot.h \
                   render.c render.h \
                   ui.c ui.h \
                   headless.c headless.h \
                   menu.c menu.h hud.c hud.h \
                   highscore.c highscore.h \
                   sfx.c sfx.h

ricochet_CPPFLAGS = -I$(top_srcdir)/libs/math \
                    -I$(top_srcdir)/libs/scene \
                    -I$(top_srcdir)/libs/raytrace \
                    -I$(top_srcdir)/libs/postfx \
                    -I$(top_srcdir)/libs/thread \
                    -I$(top_srcdir)/libs/physics \
                    $(SDL2_CFLAGS) $(GL_CFLAGS)

ricochet_LDADD = $(top_builddir)/libs/raytrace/libraytrace.la \
                 $(top_builddir)/libs/scene/libscene.la \
                 $(top_builddir)/libs/postfx/libpostfx.la \
                 $(top_builddir)/libs/physics/libphysics.la \
                 $(top_builddir)/libs/thread/libthread.la \
                 -lm -lpthread $(SDL2_LIBS) $(GL_LIBS)

if BUILD_TESTS
check_PROGRAMS = tests/check_highscore tests/check_level
tests_check_highscore_SOURCES = tests/check_highscore.c highscore.c highscore.h
tests_check_highscore_CFLAGS = $(CHECK_CFLAGS) -I$(srcdir)
tests_check_highscore_LDADD = $(CHECK_LIBS)
tests_check_level_SOURCES = tests/check_level.c level.c level.h
tests_check_level_CFLAGS = $(CHECK_CFLAGS) -I$(srcdir) -I$(top_srcdir)/libs/math
tests_check_level_LDADD = $(CHECK_LIBS) -lm
TESTS = tests/check_highscore tests/check_level
endif

dist_noinst_DATA = levels/arena.lvl levels/atrium.lvl levels/bunker.lvl \
                   levels/colonnade.lvl levels/crossroads.lvl levels/downtown.lvl \
                   levels/gauntlet.lvl levels/maze.lvl levels/pinball.lvl \
                   levels/warehouse.lvl
