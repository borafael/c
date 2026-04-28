AC_INIT([c-monorepo], [1.0])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])
AC_CONFIG_MACRO_DIRS([m4])
AM_PROG_AR               # Required for non-POSIX archivers
LT_INIT                  # Initialize Libtool for libraries
AC_PROG_CC               # Check for C compiler

PKG_CHECK_MODULES([SDL2], [sdl2])

# CHECK unit-test framework. Auto-detected so cross-compile builds
# (e.g. MinGW for Windows) succeed when no MinGW build of CHECK is
# available. Pass --enable-tests to fail configure if CHECK is missing,
# or --disable-tests to skip detection entirely.
AC_ARG_ENABLE([tests],
    AS_HELP_STRING([--enable-tests], [Enable CHECK-based unit tests (default: auto)]),
    [enable_tests=$enableval],
    [enable_tests=auto])

have_check=no
if test "x$enable_tests" != "xno"; then
    PKG_CHECK_MODULES([CHECK], [check >= 0.9.6], [have_check=yes], [have_check=no])
    if test "x$have_check" = "xno" && test "x$enable_tests" = "xyes"; then
        AC_MSG_ERROR([tests requested but CHECK >= 0.9.6 not found])
    fi
fi
AM_CONDITIONAL([BUILD_TESTS], [test "x$have_check" = "xyes"])

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

# Display OpenGL — apps blit raytracer output via FBOs and so always need
# basic GL at link time (independent of whether the OpenGL compute
# backend is built). On Windows, this is opengl32.dll's GL 1.1 entry
# points; GL >= 1.2 functions are loaded at runtime via gl_compat.
case $host_os in
    mingw*|cygwin*|msys*)
        GL_LIBS="-lopengl32"
        GL_CFLAGS=""
        ;;
    *)
        PKG_CHECK_MODULES([GL], [gl], [], [
            AC_CHECK_LIB([GL], [glViewport],
                [GL_LIBS="-lGL"; GL_CFLAGS=""],
                [AC_MSG_ERROR([OpenGL not found])])
        ])
        ;;
esac
AC_SUBST([GL_LIBS])
AC_SUBST([GL_CFLAGS])

# OpenGL compute backend in libraytrace — requires GL 4.3+. Detection
# is auto by default: present → on, absent → off quietly. Pass
# --enable-opengl to fail configure if compute is unavailable, or
# --disable-opengl to skip the backend.
#
# Windows is not yet supported (would need a runtime loader for the
# full GL 4.3 surface, not just the FBO subset gl_compat handles).
AC_ARG_ENABLE([opengl],
    AS_HELP_STRING([--enable-opengl], [Enable OpenGL compute backend (default: auto)]),
    [enable_opengl=$enableval],
    [enable_opengl=auto])

have_opengl_backend=no
if test "x$enable_opengl" != "xno"; then
    case $host_os in
        mingw*|cygwin*|msys*)
            if test "x$enable_opengl" = "xyes"; then
                AC_MSG_ERROR([OpenGL compute backend not yet supported on Windows])
            fi
            ;;
        *)
            AC_CHECK_LIB([GL], [glCreateShader],
                [have_opengl_backend=yes],
                [if test "x$enable_opengl" = "xyes"; then
                     AC_MSG_ERROR([OpenGL compute backend requested but glCreateShader not found])
                 fi])
            ;;
    esac
fi
if test "x$have_opengl_backend" = "xyes"; then
    AC_DEFINE([RT_HAVE_OPENGL_BACKEND], [1], [OpenGL compute backend is available])
fi
AM_CONDITIONAL([BUILD_OPENGL_BACKEND], [test "x$have_opengl_backend" = "xyes"])

# Tell Autoconf which Makefiles to generate
AC_CONFIG_FILES([
    Makefile
    libs/math/Makefile
    libs/thread/Makefile
    libs/physics/Makefile
    libs/scene/Makefile
    libs/raytrace/Makefile
    libs/postfx/Makefile
    libs/ini/Makefile
    libs/slice/Makefile
    libs/battleforge/Makefile
    apps/nbody/Makefile
    apps/rtdemo/Makefile
    apps/mirrors/Makefile
    apps/orb/Makefile
    apps/mech/Makefile
    apps/barrier/Makefile
    apps/anim/Makefile
    apps/pixelart/Makefile
    apps/comic/Makefile
    apps/bloom/Makefile
    apps/halftone/Makefile
    apps/toon/Makefile
])
AC_OUTPUT
