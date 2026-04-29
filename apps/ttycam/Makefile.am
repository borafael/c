bin_PROGRAMS = ttycam
ttycam_SOURCES = main.c

ttycam_CPPFLAGS = -I$(top_srcdir)/libs/term

ttycam_LDADD = $(top_builddir)/libs/term/libterm.la \
               -lm
