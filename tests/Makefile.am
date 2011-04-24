EXTRA_DIST=verify-cast.sh
AM_CPPFLAGS = $(CPPFLAGS) -I$(top_srcdir)/src

TESTS_ENVIRONMENT = PATH=$(PATH):$(buildir)
check_PROGRAMS = verifycast
TESTS = verify-cast.sh

if XDF_SUPPORT
check_PROGRAMS += syseegfile
TESTS += syseegfile
endif
if ACT2_SUPPORT
check_PROGRAMS += sysbiosemi
TESTS += sysbiosemi
endif
if GTEC_SUPPORT
check_PROGRAMS += sysgtec
TESTS += sysgtec
endif
if NSKY_SUPPORT
check_PROGRAMS += sysneurosky
TESTS += sysneurosky
endif

verifycast_SOURCES = verifycast.c
verifycast_LDADD = $(top_builddir)/src/eegdev.lo\
		   $(top_builddir)/src/eegdev-types.lo

syseegfile_SOURCES = syseegfile.c
syseegfile_LDADD = $(top_builddir)/src/libeegdev.la

sysbiosemi_SOURCES = sysbiosemi.c
sysbiosemi_LDADD = $(top_builddir)/src/libeegdev.la

sysgtec_SOURCES = sysgtec.c
sysgtec_LDADD = $(top_builddir)/src/libeegdev.la

sysneurosky_SOURCES = sysneurosky.c
sysneurosky_LDADD = $(top_builddir)/src/libeegdev.la
