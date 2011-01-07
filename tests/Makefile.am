EXTRA_DIST=verify-cast.sh
AM_CFLAGS = -I$(top_srcdir)/src

check_PROGRAMS = verifycast 
TESTS = verify-cast.sh 

verifycast_SOURCES = verifycast.c
verifycast_LDADD = $(top_builddir)/src/eegdev.lo\
		   $(top_builddir)/src/eegdev-types.lo

TESTS_ENVIRONMENT = PATH=$(PATH):$(buildir)

if USE_XDF
check_PROGRAMS += syseegfile
TESTS += syseegfile
syseegfile_SOURCES = syseegfile.c
syseegfile_LDADD = $(top_builddir)/src/libeegdev.la
endif

if USE_ACT2
check_PROGRAMS += sysbiosemi
TESTS += sysbiosemi
sysbiosemi_SOURCES = sysbiosemi.c
sysbiosemi_LDADD = $(top_builddir)/src/libeegdev.la
endif

if USE_GTEC
check_PROGRAMS += sysgtec
TESTS += sysgtec
sysgtec_SOURCES = sysgtec.c
sysgtec_LDADD = $(top_builddir)/src/libeegdev.la
endif

if USE_NSKY
check_PROGRAMS += sysneurosky
TESTS += sysneurosky
sysneurosky_SOURCES = sysneurosky.c
sysneurosky_LDADD = $(top_builddir)/src/libeegdev.la
endif
