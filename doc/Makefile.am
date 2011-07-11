EXTRA_DIST=examples/Makefile.in

AM_CPPFLAGS = -I$(top_srcdir)/src

dist_man_MANS = egd_get_data.3	egd_get_available.3	\
		egd_acq_setup.3				\
                egd_start.3 egd_stop.3			\
		egd_open.3 egd_close.3			\
		egd_get_cap.3 egd_get_numch.3		\
		egd_channel_info.3			\
		eegdev-open-options.5

exampledir = $(docdir)/examples

# Verify at least that the example compiles
eegdev_acq_SOURCES = examples/eegdev_acq.c
eegdev_acq_LDADD = $(top_builddir)/src/libeegdev.la

recinxdf_SOURCES = examples/acquisition.c examples/acquisition.h\
                   examples/recinxdf.c
recinxdf_LDADD = $(top_builddir)/src/libeegdev.la

dist_example_DATA = $(eegdev_acq_SOURCES) examples/Makefile
check_PROGRAMS = eegdev_acq

if XDF_SUPPORT
dist_example_DATA += $(recinxdf_SOURCES)
check_PROGRAMS += recinxdf
endif

