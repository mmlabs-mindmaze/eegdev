SUBDIRS = fakelibs

EXTRA_DIST=verify-cast.sh testfakegtec.sh
AM_CPPFLAGS =  -I$(top_srcdir)/src $(CPPFLAGS)

TESTS_ENVIRONMENT = PATH=$(PATH):$(builddir) \
                    EEGDEV_PROCDIR=$(top_builddir)/src \
		    EXEEXT=$(EXEEXT) builddir=$(builddir)

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
check_PROGRAMS += sysgtec testfakegtec
TESTS += testfakegtec.sh
endif
if NSKY_SUPPORT
check_PROGRAMS += sysneurosky
TESTS += sysneurosky
endif

LDADD = $(top_builddir)/src/libeegdev.la
verifycast_LDADD = $(top_builddir)/src/eegdev.lo\
		   $(top_builddir)/src/eegdev-types.lo $(EEGDEV_LIBS)
syseegfile_LDADD = $(top_builddir)/src/libeegdev.la $(EEGDEV_LIBS)
