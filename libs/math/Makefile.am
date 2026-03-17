noinst_HEADERS = vector.h

check_PROGRAMS = tests/check_vector
tests_check_vector_SOURCES = tests/check_vector.c
tests_check_vector_CFLAGS = $(CHECK_CFLAGS) -I$(srcdir)
tests_check_vector_LDADD = $(CHECK_LIBS) -lm
TESTS = tests/check_vector
