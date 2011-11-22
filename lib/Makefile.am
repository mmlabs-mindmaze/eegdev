EXTRA_DIST = timespec.h 			\
	     convfiletime.h			\
             clock_gettime.h clock_gettime.c	\
	     clock_nanosleep.h clock_nanosleep.c\
	     decl-dlfcn.h dlopen.c

noinst_LTLIBRARIES = libportability.la

libportability_la_SOURCES = portability.c
libportability_la_LIBADD = $(LTLIBOBJS)
