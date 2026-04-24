noinst_LTLIBRARIES = libscene.la

libscene_la_SOURCES = scene.c obj.c
noinst_HEADERS = scene.h obj.h

libscene_la_CPPFLAGS = -I$(top_srcdir)/libs/math \
                       -I$(top_srcdir)/libs/scene
libscene_la_LIBADD   = -lm
