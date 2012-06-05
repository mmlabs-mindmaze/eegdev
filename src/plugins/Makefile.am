pkglib_LTLIBRARIES =

AM_CPPFLAGS=$(CPPFLAGS) -I$(top_srcdir)/src/core \
            -I$(top_builddir)/lib -I$(top_srcdir)/lib
AM_LDFLAGS=-module -avoid-version -no-undefined
GNULIB = $(top_builddir)/lib/libgnu.la

if ACT2_SUPPORT
pkglib_LTLIBRARIES += biosemi.la
endif
biosemi_la_SOURCES = biosemi.c
biosemi_la_LIBADD = $(ACT2_LIBS) $(GNULIB)

if XDF_SUPPORT
pkglib_LTLIBRARIES += datafile.la
endif
datafile_la_SOURCES = fileout.c
datafile_la_LIBADD = $(XDF_LIBS) $(GNULIB) $(LIB_CLOCK_FUNCS) $(LTLIBINTL)

if NSKY_SUPPORT
pkglib_LTLIBRARIES += neurosky.la
endif
neurosky_la_SOURCES = neurosky.c
neurosky_la_LIBADD = $(NSKY_LIBS) $(GNULIB)

if GTEC_SUPPORT
pkglib_LTLIBRARIES += gtec.la
endif
gtec_la_SOURCES = gtec.c
gtec_la_LIBADD = $(GTEC_LIBS) $(GNULIB) $(LIB_CLOCK_FUNCS)

if TIA_SUPPORT
pkglib_LTLIBRARIES += tobiia.la
endif
tobiia_la_SOURCES = tobiia.c
tobiia_la_LIBADD = $(TIA_LIBS) $(GNULIB) $(LIBSOCKET) $(GETADDRINFO_LIB)
