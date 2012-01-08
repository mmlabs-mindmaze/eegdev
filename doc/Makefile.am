SUBDIRS = examples

MANPAGES = egd_get_data.3 egd_get_available.3	\
		egd_acq_setup.3				\
                egd_start.3 egd_stop.3			\
		egd_open.3 egd_close.3			\
		egd_get_cap.3 egd_get_numch.3		\
		egd_channel_info.3			\
		eegdev-open-options.5

EXTRA_DIST = $(MANPAGES)

if BUILD_CORELIB
dist_man_MANS = $(MANPAGES)
endif

