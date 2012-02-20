AM_CPPFLAGS = $(CPPFLAGS) -I$(top_srcdir)/lib

lib_LTLIBRARIES =


libeegdev_la_SOURCES = eegdev.h eegdev-pluginapi.h core.c	\
		       coreinternals.h typecast.c	\
		       opendev.c sensortypes.c

libeegdev_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined			\
                       -version-info $(CURRENT):$(REVISION):$(AGE)
libeegdev_la_LIBADD = $(top_builddir)/lib/libgnu.la $(LIB_DLOPEN)

pkgconfigdir = $(libdir)/pkgconfig

if BUILD_CORELIB
lib_LTLIBRARIES += libeegdev.la
include_HEADERS = eegdev.h eegdev-pluginapi.h
pkgconfig_DATA = eegdev.pc
endif

