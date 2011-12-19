# Verify the examples compile and execute
check_LTLIBRARIES = saw.la
check_PROGRAMS = eegdev_acq
if XDF_SUPPORT
check_PROGRAMS += recinxdf
endif

TESTS_ENVIRONMENT = EEGDEV_PLUGINS_DIR=.libs EXEEXT=$(EXEEXT)
TESTS = testexamples.sh
EXTRA_DIST=testexamples.sh

AM_CPPFLAGS = -I$(top_srcdir)/src/core -I$(top_srcdir)/lib


eegdev_acq_SOURCES = library-usage/eegdev_acq.c
eegdev_acq_LDADD = $(top_builddir)/src/core/libeegdev.la


recinxdf_SOURCES = library-usage/acquisition.c library-usage/acquisition.h\
                   library-usage/recinxdf.c
recinxdf_LDADD = $(top_builddir)/src/core/libeegdev.la -lxdffileio


saw_la_SOURCES = plugin/saw.c
saw_la_LDFLAGS=-module -avoid-version -no-undefined -rpath /nowhere
saw_la_LIBADD = $(CLOCK_LIBS) $(top_builddir)/lib/libportability.la


# install examples files
exampledir = $(docdir)/examples
nobase_dist_example_DATA = README			\
                           $(eegdev_acq_SOURCES)	\
                           $(recinxdf_SOURCES)		\
                           library-usage/Makefile	\
                           $(saw_la_SOURCES)		\
                           plugin/Makefile plugin/README

compatexampledir = $(docdir)/examples/compat
dist_compatexample_DATA = ../../config/config.h		\
                          ../../lib/portable-time.h	\
                          ../../lib/clock_gettime.c	\
			  ../../lib/clock_nanosleep.c


