AM_CPPFLAGS = $(CPPFLAGS) -I$(top_srcdir)/lib

lib_LTLIBRARIES =


libeegdev_la_SOURCES = eegdev.h eegdev-common.h core.c	\
		       coreinternals.h typecast.c	\
		       opendev.c

libeegdev_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined			\
                       -version-info $(CURRENT):$(REVISION):$(AGE)
libeegdev_la_LIBADD = $(top_builddir)/lib/libportability.la $(DLOPEN_LIBS)

pkgconfigdir = $(libdir)/pkgconfig

if BUILD_CORELIB
lib_LTLIBRARIES += libeegdev.la
include_HEADERS = eegdev.h eegdev-common.h
pkgconfig_DATA = eegdev.pc
endif

