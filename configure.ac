# If the library source code has changed at all since the last update, then increment
#      revision.
# If any interfaces have been added, removed, or changed since the last update, increment
#      current, and set revision to 0.
# If any interfaces have been added since the last public release, then increment age.
# If any interfaces have been removed since the last public release, then set age to 0.

m4_define([lib_release],0)
m4_define([lib_current],0)
m4_define([lib_revision],0)
m4_define([lib_age],0)


AC_INIT(eegdev,[lib_release.lib_current.lib_age.lib_revision])
AC_CONFIG_SRCDIR([src/eegdev.h])
AC_CONFIG_LIBOBJ_DIR([lib])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config/config.h])
AM_INIT_AUTOMAKE([no-dist-gzip dist-bzip2 color-tests])

AC_CANONICAL_HOST
AC_CANONICAL_BUILD

AC_PROG_CC_C99
AM_PROG_CC_C_O

LT_INIT([win32-dll disable-static])
AC_SUBST(LIBTOOL_DEPS)


os_support=other
case $host in
	*win32* | *mingw32* | *cygwin* | *windows*)
		AM_LDFLAGS="$AM_LDFLAGS -no-undefined"
		AC_DEFINE([HAVE_GETSYSTEMTIMEASFILETIME], [1], [define to 1 if you have GetSytemTimeAsFileTime function])
		os_support=win32
		;;
esac

AC_SEARCH_LIBS([usb_open], [usb], [], AC_MSG_ERROR([The library libusb version 0.1.12 or the compatibility layer must be installed]))
AC_SEARCH_LIBS([pthread_create], [pthread posix4], 
               [], AC_MSG_ERROR([The pthread library has not been found]))
AC_SEARCH_LIBS([xdf_open], [xdffileio], [], AC_MSG_ERROR([The library xdffileio must be installed]))
AC_SEARCH_LIBS([clock_gettime], [rt posix4])
AC_SEARCH_LIBS([clock_nanosleep], [rt posix4])
AC_CHECK_TYPES([struct timespec, clockid_t])
AC_CHECK_DECLS([clock_gettime, clock_nanosleep],,,[#include <time.h>])
AC_CHECK_FUNCS([nanosleep gettimeofday ftime _ftime])
AC_REPLACE_FUNCS([clock_gettime clock_nanosleep])

AC_SUBST([RELEASE],[lib_release])
AC_SUBST([CURRENT],[lib_current])
AC_SUBST([REVISION],[lib_revision])
AC_SUBST([AGE],[lib_age])

AC_SUBST(AM_LDFLAGS)

AC_CONFIG_FILES([Makefile lib/Makefile src/Makefile tests/Makefile doc/Makefile])
AC_OUTPUT

