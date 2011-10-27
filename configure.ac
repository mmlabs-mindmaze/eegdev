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
AC_INIT(eegdev,[lib_current.lib_revision],[nicolas.bourdaud@epfl.ch])
AC_CONFIG_SRCDIR([src/core/eegdev.h])
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
LT_INIT([dlopen win32-dll disable-static])
AC_SUBST(LIBTOOL_DEPS)

AC_DEF_API_EXPORT_ATTRS

# Test for the correct way to set CLOEXEC to file descriptors
AC_CHECK_DECLS([O_CLOEXEC, FD_CLOEXEC], [], [], [[#include <fcntl.h>]])


# pthread is needed in core and modules
AC_SEARCH_LIBS([pthread_create], [pthread posix4], [],
               AC_MSG_ERROR([The pthread library has not been found]))

# Core library checks
save_LIBS=$LIBS
AC_SEARCH_LIBS([dlopen], [dl], 
               [], AC_MSG_ERROR([dlopen unsupported]))
AC_SUBST([CORE_LIBS], "$LIBS")
LIBS=$save_LIBS



# EEGfile support
save_LIBS=$LIBS
save_LTLIBOBJS=$LTLIBOBJS
AC_ARG_WITH([xdf], AC_HELP_STRING([--with-xdf],
	               [Support for EEGfile backend @<:@default=check@:>@]),
	    [], [with_xdf=check])
AS_IF([test "x$with_xdf" != xno], 
     [AC_SEARCH_LIBS([xdf_open], [xdffileio], [xdf_support=yes],
        [xdf_support=no; if test "x$with_xdf" != xcheck; then
            AC_MSG_FAILURE([xdffileio library required for EEGfile support])
         fi])
      AC_SEARCH_LIBS([regcomp], [regex regexp], [xdf_support="$xdf_support"],
        [xdf_support=no; if test "x$with_xdf" != xcheck; then
             AC_MSG_FAILURE([regex library required for EEGfile support])
         fi])
     # Check for replacement of clock_*
     AC_SEARCH_LIBS([clock_gettime], [rt posix4])
     AC_SEARCH_LIBS([clock_nanosleep], [rt posix4])
     AC_CHECK_TYPES([struct timespec, clockid_t])
     AC_CHECK_DECLS([clock_gettime, clock_nanosleep],,,[#include <time.h>])
     AC_CHECK_FUNCS([nanosleep gettimeofday ftime _ftime posix_memalign])
     AC_CHECK_FUNC_FNARG([GetSystemTimeAsFileTime], [0], [#include <windows.h>])
     AC_REPLACE_FUNCS([clock_gettime clock_nanosleep])],
     [xdf_support=no])
AM_CONDITIONAL([XDF_SUPPORT], [test "x$xdf_support" = "xyes"])
AS_IF([test "x$xdf_support" = "xyes"], optional_examples=recinxdf$EXEEXT)
AC_SUBST([XDF_LIBS], "$LIBS")
AC_SUBST([XDF_LTLIBOBJS], "$LTLIBOBJS")
LIBS=$save_LIBS
LTLIBOBJS=$save_LTLIBOBJS



# Biosemi support
save_LIBS=$LIBS
AC_ARG_WITH([act2], AC_HELP_STRING([--with-act2],
	               [Support for Biosemi backend @<:@default=check@:>@]),
	    [], [with_act2=check])
AS_IF([test "x$with_act2" != xno],
    [PKG_CHECK_MODULES(LIBUSB, [libusb-1.0], [act2_support=yes],
        [act2_support=no; if test "x$with_act2" != xcheck; then
             AC_MSG_FAILURE([libusb-1.0 library required for Biosemi support])
         fi])],
    [act2_support=no])
AM_CONDITIONAL([ACT2_SUPPORT], [test "x$act2_support" = xyes])
AC_SUBST([ACT2_LIBS], "$LIBUSB_LIBS $LIBS")
AC_SUBST([CPPFLAGS], "$LIBUSB_CFLAGS $CPPFLAGS")
LIBS=$save_LIBS



# gTec support
save_LIBS=$LIBS
AC_ARG_WITH([gtec], AC_HELP_STRING([--with-gtec],
		         [Support for gTec backend @<:@default=check@:>@]),
	    [], [with_gtec=check])
gtec_support=no
AS_IF([test "x$with_gtec" != xno],
      [AC_SEARCH_LIBS([GT_OpenDevice], [gUSBampAPI gusbampapi], [gtec_support=yes])])
AS_IF([test "x$with_gtec" != xcheck && test "x$with_gtec" != xno],
      AS_IF([test "x$gtec_support" = xno], 
            AC_MSG_FAILURE([gUSBampAPI library required for gTec support])))
AM_CONDITIONAL([GTEC_SUPPORT], [test "x$gtec_support" = xyes])
AC_SUBST([GTEC_LIBS], "$LIBS")
LIBS=$save_LIBS



# Neurosky support
save_LIBS=$LIBS
AC_ARG_WITH([neurosky], AC_HELP_STRING([--with-neurosky],
	                [Support for Neurosky backend @<:@default=no@:>@]),
	    [], [with_neurosky=no])
neurosky_support=$with_neurosky
AM_CONDITIONAL([NSKY_SUPPORT], [test "x$neurosky_support" = xyes])
AC_SUBST([NSKY_LIBS], "$LIBS")
LIBS=$save_LIBS


AX_DEFINE_DIR([LIBDIR], [libdir], [Installed library path])

AC_SUBST([optional_examples])
AC_SUBST([CURRENT],[lib_current])
AC_SUBST([REVISION],[lib_revision])
AC_SUBST([AGE],[lib_age])

AC_CONFIG_FILES([Makefile lib/Makefile
                 src/core/Makefile src/core/eegdev.pc
		 src/plugins/Makefile
                 tests/Makefile tests/fakelibs/Makefile
		 doc/Makefile doc/egd_open.3 doc/examples/Makefile])
AC_OUTPUT

echo "

Configuration summary
=====================
    EEG file support : $xdf_support
    Biosemi support  : $act2_support
    gTec support     : $gtec_support
    Neurosky support : $neurosky_support
"

