lib_LTLIBRARIES = libeegdev.la
include_HEADERS = eegdev.h

AM_CPPFLAGS = $(CPPFLAGS)
libeegdev_la_SOURCES = eegdev.h eegdev-common.h eegdev.c	\
		       eegdev-types.h eegdev-types.c		\
		       device-helper.h device-helper.c		\
		       devices.h eegdev-open.c


if ACT2_SUPPORT
libeegdev_la_SOURCES += biosemi.c usb_comm.h usb_comm.c
AM_CPPFLAGS += -DACT2_SUPPORT
endif

if XDF_SUPPORT
libeegdev_la_SOURCES += fileout.c
AM_CPPFLAGS += -DXDF_SUPPORT
endif

if NSKY_SUPPORT
libeegdev_la_SOURCES += neurosky.c
AM_CPPFLAGS += -DNSKY_SUPPORT
endif

if GTEC_SUPPORT
AM_CPPFLAGS += -DGTEC_SUPPORT
libeegdev_la_SOURCES += gtec.c
endif

libeegdev_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined			\
                       -version-info $(CURRENT):$(REVISION):$(AGE)
libeegdev_la_LIBADD = $(LTLIBOBJS) $(EEGDEV_LIBS)
libeegdev_la_DEPENDENCIES = $(LTLIBOBJS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = eegdev.pc
