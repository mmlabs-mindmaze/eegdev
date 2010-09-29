EXTRA_DIST=verify-cast.sh
AM_CFLAGS = -I$(top_srcdir)/src

check_PROGRAMS = verifycast syseegfile sysbiosemi sysneurosky

sysbiosemi_SOURCES = sysbiosemi.c
sysbiosemi_LDADD = $(top_builddir)/src/libeegdev.la
sysneurosky_SOURCES = sysneurosky.c
sysneurosky_LDADD = $(top_builddir)/src/libeegdev.la
syseegfile_SOURCES = syseegfile.c
syseegfile_LDADD = $(top_builddir)/src/libeegdev.la
verifycast_SOURCES = verifycast.c
verifycast_LDADD = $(top_builddir)/src/eegdev.lo\
		   $(top_builddir)/src/eegdev-types.lo

TESTS_ENVIRONMENT = PATH=$(PATH):$(buildir)
TESTS = verify-cast.sh syseegfile sysbiosemi sysneurosky
