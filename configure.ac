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

# Tell Autoconf which Makefiles to generate
AC_CONFIG_FILES([
    Makefile
    libs/math/Makefile
    libs/thread/Makefile
    libs/raytrace/Makefile
    libs/ini/Makefile
    libs/slice/Makefile
    libs/battleforge/Makefile
    apps/nbody/Makefile
    apps/rtdemo/Makefile
    apps/barrier/Makefile
])
AC_OUTPUT
