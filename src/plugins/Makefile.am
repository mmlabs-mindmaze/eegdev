pkglib_LTLIBRARIES =

AM_CPPFLAGS=$(CPPFLAGS) -I$(top_srcdir)/src/core -I$(top_srcdir)/lib -I$(top_srcdir)
AM_LDFLAGS=-module -avoid-version -no-undefined

if ACT2_SUPPORT
pkglib_LTLIBRARIES += biosemi.la
endif
biosemi_la_SOURCES = biosemi.c usb_comm.h usb_comm.c
biosemi_la_LIBADD = $(ACT2_LIBS)

if XDF_SUPPORT
pkglib_LTLIBRARIES += datafile.la
endif
datafile_la_SOURCES = fileout.c
datafile_la_LIBADD = $(top_builddir)/lib/libgnu.la $(CLOCK_LIBS) $(XDF_LIBS)

if NSKY_SUPPORT
pkglib_LTLIBRARIES += neurosky.la
endif
neurosky_la_SOURCES = neurosky.c
neurosky_la_LIBADD = $(NSKY_LIBS)

if GTEC_SUPPORT
pkglib_LTLIBRARIES += gtec.la
endif
gtec_la_SOURCES = gtec.c device-helper.h device-helper.c
gtec_la_LIBADD = $(top_builddir)/lib/libgnu.la $(CLOCK_LIBS) $(GTEC_LIBS)

if TIA_SUPPORT
pkglib_LTLIBRARIES += tobiia.la
endif
tobiia_la_SOURCES = tobiia.c cross-socket.h device-helper.h device-helper.c
tobiia_la_LIBADD = $(TIA_LIBS)
