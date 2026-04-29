bin_PROGRAMS = tty3d
tty3d_SOURCES = main.c

tty3d_CPPFLAGS = -I$(top_srcdir)/libs/math \
                 -I$(top_srcdir)/libs/scene \
                 -I$(top_srcdir)/libs/raytrace \
                 -I$(top_srcdir)/libs/thread \
                 -I$(top_srcdir)/libs/term

tty3d_LDADD = $(top_builddir)/libs/term/libterm.la \
              $(top_builddir)/libs/raytrace/libraytrace.la \
              $(top_builddir)/libs/scene/libscene.la \
              $(top_builddir)/libs/thread/libthread.la \
              -lm -lpthread
