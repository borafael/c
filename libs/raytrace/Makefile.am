noinst_LTLIBRARIES = libraytrace.la

libraytrace_la_SOURCES = renderer.c scene_accel.c gl_compat.c

if BUILD_CPU_BACKEND
libraytrace_la_SOURCES += cpu/renderer.c cpu/render_chunk.c \
                          cpu/sphere.c cpu/plane.c cpu/disc.c \
                          cpu/cylinder.c cpu/cone.c cpu/torus.c \
                          cpu/triangle.c cpu/box.c \
                          cpu/sprite.c cpu/heightfield.c \
                          cpu/mesh.c cpu/bvh.c
endif

noinst_HEADERS = viewport.h renderer.h scene_accel.h gl_compat.h \
                 sphere.h plane.h disc.h cylinder.h cone.h torus.h \
                 triangle.h box.h geodesic.h \
                 sprite.h heightfield.h mesh.h \
                 cpu/render_chunk.h cpu/bvh.h
libraytrace_la_CPPFLAGS = -I$(top_srcdir)/libs/math \
                          -I$(top_srcdir)/libs/scene \
                          -I$(top_srcdir)/libs/thread \
                          -I$(top_srcdir)/libs/raytrace \
                          $(GL_CFLAGS)
libraytrace_la_LIBADD = $(top_builddir)/libs/scene/libscene.la -lm

if BUILD_OPENGL_BACKEND
libraytrace_la_SOURCES += opengl/renderer.c
libraytrace_la_CPPFLAGS += $(GL_CFLAGS)
libraytrace_la_LIBADD += $(GL_LIBS)
endif

if BUILD_TESTS
check_PROGRAMS = tests/check_geodesic
tests_check_geodesic_SOURCES = tests/check_geodesic.c
tests_check_geodesic_CFLAGS  = $(CHECK_CFLAGS) \
                               -I$(srcdir) \
                               -I$(top_srcdir)/libs/math \
                               -I$(top_srcdir)/libs/scene
tests_check_geodesic_LDADD   = $(top_builddir)/libs/scene/libscene.la \
                               $(CHECK_LIBS) -lm
TESTS = tests/check_geodesic
endif

if BUILD_VULKAN_BACKEND
libraytrace_la_SOURCES += vulkan/renderer.c
libraytrace_la_CPPFLAGS += $(VULKAN_CFLAGS) -I$(builddir)/vulkan
libraytrace_la_LIBADD += $(VULKAN_LIBS)
BUILT_SOURCES = vulkan/raytrace_spv.h
CLEANFILES = vulkan/raytrace.spv vulkan/raytrace_spv.h

vulkan/raytrace.spv: $(srcdir)/vulkan/raytrace.comp
	@$(MKDIR_P) vulkan
	$(GLSLC) --target-env=vulkan1.1 -fshader-stage=compute -O -o $@ $<

vulkan/raytrace_spv.h: vulkan/raytrace.spv
	xxd -i -name raytrace_spv $< > $@
endif
