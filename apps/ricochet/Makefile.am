bin_PROGRAMS = ricochet
ricochet_SOURCES = main.c sfx.c sfx.h hud.c hud.h menu.c menu.h \
                   highscore.c highscore.h

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
check_PROGRAMS = tests/check_highscore
tests_check_highscore_SOURCES = tests/check_highscore.c highscore.c highscore.h
tests_check_highscore_CFLAGS = $(CHECK_CFLAGS) -I$(srcdir)
tests_check_highscore_LDADD = $(CHECK_LIBS)
TESTS = tests/check_highscore
endif
