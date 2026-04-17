noinst_LTLIBRARIES = libphysics.la
libphysics_la_SOURCES = physics.c physics.h
libphysics_la_CPPFLAGS = -I$(top_srcdir)/libs/math -I$(top_srcdir)/libs/thread
libphysics_la_LIBADD = $(top_builddir)/libs/thread/libthread.la -lm

check_PROGRAMS = tests/check_physics
tests_check_physics_SOURCES = tests/check_physics.c
tests_check_physics_CFLAGS = $(CHECK_CFLAGS) -I$(srcdir) -I$(top_srcdir)/libs/math
tests_check_physics_LDADD = libphysics.la $(top_builddir)/libs/thread/libthread.la $(CHECK_LIBS) -lm
TESTS = tests/check_physics
