EXTRA_DIST=exports.sym

lib_LTLIBRARIES = libeegdev.la
include_HEADERS = eegdev.h

libeegdev_la_SOURCES = eegdev.h eegdev-common.h eegdev.c	\
		       eegdev-types.h eegdev-types.c		\
		       biosemi.h gtec.h neurosky.h fileout.h	\
		       eegdev-open.c

if USE_XDF
libeegdev_la_SOURCES += fileout.c
endif
if USE_ACT2
libeegdev_la_SOURCES += biosemi.c usb_comm.h usb_comm.c
endif
if USE_GTEC
libeegdev_la_SOURCES += gtec.c
endif
if USE_NSKY
libeegdev_la_SOURCES += neurosky.c
endif


libeegdev_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined			\
                       -export-symbols $(srcdir)/exports.sym 		\
                       -version-info $(CURRENT):$(REVISION):$(AGE)
libeegdev_la_LIBADD = $(LTLIBOBJS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = eegdev.pc
