AC_INIT([c-monorepo], [1.0])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])
AC_CONFIG_MACRO_DIRS([m4])
AM_PROG_AR               # Required for non-POSIX archivers
LT_INIT                  # Initialize Libtool for libraries
AC_PROG_CC               # Check for C compiler

PKG_CHECK_MODULES([SDL2], [sdl2])
PKG_CHECK_MODULES([CHECK], [check >= 0.9.6])

# Generate config.h with AC_DEFINE results
AC_CONFIG_HEADERS([config.h])

# Raytrace backends. Each backend has an AM_CONDITIONAL that gates
# whether its source files are compiled into libraytrace, and an
# AC_DEFINE that the factory uses to conditionally include the
# backend's case arm at compile time.
#
# CPU is always available — no configure flag needed. Future backends
# (OpenGL, Vulkan, CUDA) will add --enable-<name> flags gated on
# PKG_CHECK_MODULES results.
AC_DEFINE([RT_HAVE_CPU_BACKEND], [1], [CPU backend is always built])
AM_CONDITIONAL([BUILD_CPU_BACKEND], [true])

# OpenGL backend — requires GL 4.3+ for compute shaders. Detection is
# auto by default: present → on, absent → off quietly. Pass
# --enable-opengl to fail configure if GL can't be found, or
# --disable-opengl to skip detection entirely.
AC_ARG_ENABLE([opengl],
    AS_HELP_STRING([--enable-opengl], [Enable OpenGL compute backend (default: auto)]),
    [enable_opengl=$enableval],
    [enable_opengl=auto])

have_opengl=no
if test "x$enable_opengl" != "xno"; then
    PKG_CHECK_MODULES([GL], [gl], [have_opengl=yes], [
        AC_CHECK_LIB([GL], [glCreateShader],
            [have_opengl=yes; GL_LIBS="-lGL"; GL_CFLAGS=""],
            [have_opengl=no])
    ])
    if test "x$have_opengl" = "xno" && test "x$enable_opengl" = "xyes"; then
        AC_MSG_ERROR([OpenGL requested but not found])
    fi
fi

if test "x$have_opengl" = "xyes"; then
    AC_DEFINE([RT_HAVE_OPENGL_BACKEND], [1], [OpenGL compute backend is available])
fi
AM_CONDITIONAL([BUILD_OPENGL_BACKEND], [test "x$have_opengl" = "xyes"])

# Tell Autoconf which Makefiles to generate
AC_CONFIG_FILES([
    Makefile
    libs/math/Makefile
    libs/thread/Makefile
    libs/physics/Makefile
    libs/scene/Makefile
    libs/raytrace/Makefile
    libs/ini/Makefile
    libs/slice/Makefile
    libs/battleforge/Makefile
    apps/nbody/Makefile
    apps/rtdemo/Makefile
    apps/mirrors/Makefile
    apps/orb/Makefile
    apps/mech/Makefile
    apps/barrier/Makefile
])
AC_OUTPUT
