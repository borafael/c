noinst_HEADERS = vector.h matrix.h

if BUILD_TESTS
check_PROGRAMS = tests/check_vector tests/check_matrix
tests_check_vector_SOURCES = tests/check_vector.c
tests_check_vector_CFLAGS = $(CHECK_CFLAGS) -I$(srcdir)
tests_check_vector_LDADD = $(CHECK_LIBS) -lm
tests_check_matrix_SOURCES = tests/check_matrix.c
tests_check_matrix_CFLAGS = $(CHECK_CFLAGS) -I$(srcdir)
tests_check_matrix_LDADD = $(CHECK_LIBS) -lm
TESTS = tests/check_vector tests/check_matrix
endif
