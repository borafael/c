AC_INIT([c-monorepo], [1.0])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])
AC_CONFIG_MACRO_DIRS([m4])
AM_PROG_AR               # Required for non-POSIX archivers
LT_INIT                  # Initialize Libtool for libraries
AC_PROG_CC               # Check for C compiler

PKG_CHECK_MODULES([SDL2], [sdl2])
PKG_CHECK_MODULES([CHECK], [check >= 0.9.6])

# Tell Autoconf which Makefiles to generate
AC_CONFIG_FILES([
    Makefile
    libs/math/Makefile
    libs/thread/Makefile
    libs/raytrace/Makefile
    libs/slice/Makefile
    libs/battleforge/Makefile
    apps/nbody/Makefile
    apps/rtdemo/Makefile
    apps/battleforge/Makefile
])
AC_OUTPUT
