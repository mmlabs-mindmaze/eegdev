AUTOMAKE_OPTIONS = subdir-objects
AM_CFLAGS = -I$(top_srcdir)/lib

lib_LTLIBRARIES = libeegdev.la
include_HEADERS = eegdev.h

libeegdev_la_SOURCES = eegdev.h eegdev-common.h eegdev.c	\
		       eegdev-types.h eegdev-types.c		\
		       fileout.c				\
                       biosemi.c				

