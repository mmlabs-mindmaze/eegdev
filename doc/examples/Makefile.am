AM_CPPFLAGS = -I$(top_srcdir)/src/core -I$(top_srcdir)/lib

exampledir = $(docdir)/examples

# Verify the examples compile
check_PROGRAMS = eegdev_acq
if XDF_SUPPORT
check_PROGRAMS += recinxdf
endif


eegdev_acq_SOURCES = library-usage/eegdev_acq.c
eegdev_acq_LDADD = $(top_builddir)/src/core/libeegdev.la


recinxdf_SOURCES = library-usage/acquisition.c library-usage/acquisition.h\
                   library-usage/recinxdf.c
recinxdf_LDADD = $(top_builddir)/src/core/libeegdev.la -lxdffileio


nobase_dist_example_DATA = $(eegdev_acq_SOURCES)	\
                           $(recinxdf_SOURCES)		\
                           library-usage




