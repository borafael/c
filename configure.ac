AC_INIT([c-monorepo], [1.0])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AC_CONFIG_MACRO_DIRS([m4])
AM_PROG_AR               # Required for non-POSIX archivers
LT_INIT                  # Initialize Libtool for libraries
AC_PROG_CC               # Check for C compiler

PKG_CHECK_MODULES([SDL2], [sdl2])

# Tell Autoconf which Makefiles to generate
AC_CONFIG_FILES([
    Makefile
    libs/math/Makefile
    libs/thread/Makefile
    libs/raytrace/Makefile
    apps/nbody/Makefile
])
AC_OUTPUT
