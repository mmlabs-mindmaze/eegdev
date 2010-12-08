# - If the library source code has changed at all since the last update,
# then increment revision.
# - If any interfaces have been added, removed, or changed since the last
# update, increment current, and set revision to 0.
# - If any interfaces have been added since the last public release, then
# increment age.
# - If any interfaces have been removed since the last public release, then
# set age to 0.

m4_define([lib_current],0)
m4_define([lib_revision],0)
m4_define([lib_age],0)

# Setup autoconf
AC_INIT(eegdev,[lib_current.lib_age.lib_revision],
        [nicolas.burdaud@epfl.ch])
AC_CONFIG_SRCDIR([src/eegdev.h])
AC_CONFIG_LIBOBJ_DIR([lib])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config/config.h])

# Setup automake
AM_INIT_AUTOMAKE([no-dist-gzip dist-bzip2 color-tests subdir-objects])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])


AC_CANONICAL_HOST
AC_CANONICAL_BUILD

# Check for programs
AC_PROG_CC_C99
AM_PROG_CC_C_O

LT_INIT([win32-dll disable-static])
AC_SUBST(LIBTOOL_DEPS)

AC_DEF_API_EXPORT_ATTRS

# Necessary libraries checks
AC_SEARCH_LIBS([libusb_submit_transfer], [usb-1.0], 
               [], AC_MSG_ERROR([The library libusb-1.0 must be installed]))
AC_SEARCH_LIBS([pthread_create], [pthread posix4], 
               [], AC_MSG_ERROR([The pthread library has not been found]))
AC_SEARCH_LIBS([xdf_open], [xdffileio], 
               [], AC_MSG_ERROR([The library xdffileio must be installed]))
AC_SEARCH_LIBS([regcomp], [regex regexp], 
               [], AC_MSG_ERROR([The library regex must be installed]))
AC_SEARCH_LIBS([GT_OpenDevice], [gUSBampAPI], 
               [gtec=true], [gtec=false])

if test "x$gtec" = "xtrue" ; then
	AC_DEFINE([GTEC_SUPPORT], [1], [Define to 1 if the support for gTec system should be built in])
fi
AM_CONDITIONAL(USE_GTEC, test "x$gtec" = "xtrue")

# Replacement checks
AC_SEARCH_LIBS([clock_gettime], [rt posix4])
AC_SEARCH_LIBS([clock_nanosleep], [rt posix4])
AC_CHECK_TYPES([struct timespec, clockid_t])
AC_CHECK_DECLS([clock_gettime, clock_nanosleep],,,[#include <time.h>])
AC_CHECK_FUNCS([nanosleep gettimeofday ftime _ftime posix_memalign])
AC_CHECK_FUNC_FNARG([GetSystemTimeAsFileTime], [0], [#include <windows.h>])
AC_REPLACE_FUNCS([clock_gettime clock_nanosleep])

AC_SUBST([CURRENT],[lib_current])
AC_SUBST([REVISION],[lib_revision])
AC_SUBST([AGE],[lib_age])

AC_CONFIG_FILES([Makefile lib/Makefile src/Makefile src/eegdev.pc
                 tests/Makefile doc/Makefile])
AC_OUTPUT

if test x$gtec = xfalse
then
echo "Configured without gTec"
fi

