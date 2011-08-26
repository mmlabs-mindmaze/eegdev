lib_LTLIBRARIES = libeegdev.la
noinst_LTLIBRARIES = 
libexec_PROGRAMS = 
include_HEADERS = eegdev.h

AM_CPPFLAGS = $(CPPFLAGS)
libeegdev_la_SOURCES = eegdev.h eegdev-common.h eegdev.c	\
		       eegdev-types.h eegdev-types.c		\
		       device-helper.h device-helper.c		\
		       devices.h eegdev-open.c

if BUILD_PROCDEV
libeegdev_la_SOURCES +=  eegdev-procdev.h procdev-common.h procdev-parent.c
noinst_LTLIBRARIES += libchild.la
endif

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
if GTEC_CHILD
libeegdev_la_SOURCES += gtec-procdev.c
libexec_PROGRAMS += eegdev-proc-gtec
else
libeegdev_la_SOURCES += gtec.c
endif
endif

libeegdev_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined			\
                       -version-info $(CURRENT):$(REVISION):$(AGE)
libeegdev_la_LIBADD = $(LTLIBOBJS) $(EEGDEV_LIBS)
libeegdev_la_DEPENDENCIES = $(LTLIBOBJS)

libchild_la_SOURCES = eegdev-procdev.h procdev-common.h procdev-child.c \
		      device-helper.h device-helper.c		\
                      eegdev-common.h eegdev-types.h
		      
libchild_la_CFLAGS = $(CFLAGS32)
libchild_la_CPPFLAGS = $(CPPFLAGS32)
libchild_la_LIBADD = $(EEGDEV32_LIBS)

eegdev_proc_gtec_SOURCES = gtec-procdev.c gtec.c
eegdev_proc_gtec_CFLAGS = $(CFLAGS32)
eegdev_proc_gtec_CPPFLAGS = $(CPPFLAGS32)
eegdev_proc_gtec_LDADD = libchild.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = eegdev.pc
