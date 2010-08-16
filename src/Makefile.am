AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libeegdev.la
include_HEADERS = eegdev.h

libeegdev_la_SOURCES = eegdev.h eegdev-common.h eegdev.c	\
		       eegdev-types.h eegdev-types.c		\
		       fileout.c				\
                       biosemi.c				
libeegdev_la_LIBADD = $(LTLIBOBJS)

