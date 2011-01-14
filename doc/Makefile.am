AM_CFLAGS = -I$(top_srcdir)/src

dist_man_MANS = egd_get_data.3	egd_get_available.3	\
		egd_acq_setup.3				\
                egd_start.3 egd_stop.3			\
		egd_close.3				\
		egd_get_cap.3 egd_get_numch.3		\
		egd_channel_info.3			

nobase_dist_doc_DATA = example/Makefile example/eegdev_acq.c

# Verify at least that the example compiles
check_PROGRAMS = eegdev_acq
eegdev_acq_SOURCES = example/eegdev_acq.c
eegdev_acq_LDADD = $(top_builddir)/src/libeegdev.la

