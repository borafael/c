noinst_LTLIBRARIES = libscene.la libufbx.la

# ufbx is vendored third-party. Compile it as a separate convenience
# library so we can relax -Werror for its translation unit without
# weakening the rest of the scene build.
libufbx_la_SOURCES  = vendor/ufbx/ufbx.c vendor/ufbx/ufbx.h
libufbx_la_CPPFLAGS = -I$(top_srcdir)/libs/scene/vendor/ufbx
libufbx_la_CFLAGS   = -w

libscene_la_SOURCES = scene.c obj.c fbx.c texture_load.c
noinst_HEADERS = scene.h obj.h fbx.h

libscene_la_CPPFLAGS = -I$(top_srcdir)/libs/math \
                       -I$(top_srcdir)/libs/scene \
                       -I$(top_srcdir)/libs/scene/vendor/ufbx \
                       -I$(top_srcdir)/libs/slice
libscene_la_LIBADD   = libufbx.la -lm

if BUILD_TESTS
check_PROGRAMS = tests/check_fbx
tests_check_fbx_SOURCES = tests/check_fbx.c
tests_check_fbx_CFLAGS  = $(CHECK_CFLAGS) \
                          -I$(srcdir) \
                          -I$(top_srcdir)/libs/math
tests_check_fbx_LDADD   = libscene.la $(CHECK_LIBS)
TESTS = tests/check_fbx
endif

EXTRA_DIST = tests/cube.fbx
