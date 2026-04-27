noinst_LTLIBRARIES = libini.la
libini_la_SOURCES = ini.c ini.h

if BUILD_TESTS
check_PROGRAMS = tests/check_ini
tests_check_ini_SOURCES = tests/check_ini.c
tests_check_ini_CFLAGS = $(CHECK_CFLAGS) -I$(srcdir)
tests_check_ini_LDADD = libini.la $(CHECK_LIBS)
TESTS = tests/check_ini
endif

EXTRA_DIST = tests/test.ini
