SUBDIRS = examples

MANPAGES = egd_get_data.3 egd_get_available.3	\
		egd_acq_setup.3				\
                egd_start.3 egd_stop.3			\
		egd_close.3				\
		egd_get_cap.3 egd_get_numch.3		\
		egd_channel_info.3			\
		egd_sensor_name.3 egd_sensor_type.3	\
		eegdev-open-options.5

EXTRA_DIST = $(MANPAGES) \
             eegdev-biosemi.5 eegdev-datafile.5 \
             eegdev-neurosky.5 eegdev-gtec.5 eegdev-tobiia.5 \
	     eegdev-bbt.5 eegdev-barv.5	
man_MANS =

if BUILD_CORELIB
man_MANS += $(MANPAGES)
nodist_man_MANS = egd_open.3
endif

if ACT2_SUPPORT
man_MANS += eegdev-biosemi.5
endif

if XDF_SUPPORT
man_MANS += eegdev-datafile.5
endif

if NSKY_SUPPORT
man_MANS += eegdev-neurosky.5
endif

if GTEC_SUPPORT
man_MANS += eegdev-gtec.5
endif

if TIA_SUPPORT
man_MANS += eegdev-tobiia.5
endif

if BBT_SUPPORT
man_MANS += eegdev-bbt.5
endif

if BARV_SUPPORT
man_MANS += eegdev-barv.5
endif

if GTECNET_SUPPORT
man_MANS += eegdev-gtecnet.5
endif

if ARDUINO_SUPPORT
man_MANS += eegdev-arduino.5
endif
