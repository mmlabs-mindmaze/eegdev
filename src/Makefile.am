lib_LTLIBRARIES = libeegdev.la
pkglib_LTLIBRARIES =
include_HEADERS = eegdev.h

AM_CPPFLAGS = $(CPPFLAGS)
libeegdev_la_SOURCES = eegdev.h eegdev-common.h eegdev.c	\
		       eegdev-types.h eegdev-types.c		\
		       eegdev-open.c

libeegdev_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined			\
                       -version-info $(CURRENT):$(REVISION):$(AGE)
libeegdev_la_LIBADD = $(LTLIBOBJS) $(CORE_LIBS)
libeegdev_la_DEPENDENCIES = $(LTLIBOBJS)

if ACT2_SUPPORT
pkglib_LTLIBRARIES += biosemi.la
endif
biosemi_la_SOURCES = biosemi.c usb_comm.h usb_comm.c
biosemi_la_LIBADD = $(ACT2_LIBS)
biosemi_la_LDFLAGS = -module -avoid-version -no-undefined

if XDF_SUPPORT
pkglib_LTLIBRARIES += datafile.la
endif
datafile_la_SOURCES = fileout.c
datafile_la_LIBADD = $(XDF_LTLIBOBJS) $(XDF_LIBS)
datafile_la_LDFLAGS = -module -avoid-version -no-undefined
datafile_la_DEPENDENCIES = $(XDF_LTLIBOBJS)

if NSKY_SUPPORT
pkglib_LTLIBRARIES += neurosky.la
endif
neurosky_la_SOURCES = neurosky.c
neurosky_la_LIBADD = $(NSKY_LIBS)
neurosky_la_LDFLAGS = -module -avoid-version -no-undefined

if GTEC_SUPPORT
pkglib_LTLIBRARIES += gtec.la
endif
gtec_la_SOURCES = gtec.c device-helper.h device-helper.c
gtec_la_LIBADD = $(GTEC_LIBS)
gtec_la_LDFLAGS = -module -avoid-version -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = eegdev.pc
