SUBDIRS = examples

MANPAGES = eegdev-open-options.5

EXTRA_DIST = $(MANPAGES) \
             eegdev-biosemi.5 eegdev-datafile.5 \
             eegdev-neurosky.5 eegdev-gtec.5 eegdev-tobiia.5
man_MANS =

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
