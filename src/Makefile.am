EXTRA_DIST=exports.sym

lib_LTLIBRARIES = libeegdev.la
include_HEADERS = eegdev.h

libeegdev_la_SOURCES = eegdev.h eegdev-common.h eegdev.c	\
		       eegdev-types.h eegdev-types.c		\
		       fileout.c				\
                       biosemi.c				\
		       neurosky.c				\
                       gtec.c
if USE_ACT2
libeegdev_la_SOURCES += usb_comm.h usb_comm.c
endif

libeegdev_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined			\
                       -export-symbols $(srcdir)/exports.sym 		\
                       -version-info $(CURRENT):$(REVISION):$(AGE)
libeegdev_la_LIBADD = $(LTLIBOBJS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = eegdev.pc
