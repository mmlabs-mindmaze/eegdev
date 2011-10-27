SUBDIRS = fakelibs

EXTRA_DIST=verify-cast.sh testfakegtec.sh
AM_CPPFLAGS =  -I$(top_srcdir)/src $(CPPFLAGS)

TESTS_ENVIRONMENT = PATH=$(PATH):$(builddir) \
                    EEGDEV_PLUGINS_DIR=$(top_builddir)/src/.libs \
		    EXEEXT=$(EXEEXT) builddir=$(builddir)

check_PROGRAMS = verifycast verifysplit
TESTS = verify-cast.sh verifysplit

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
TESTS += testfakegtec.sh
endif
if NSKY_SUPPORT
check_PROGRAMS += sysneurosky
TESTS += sysneurosky
endif

LDADD = $(top_builddir)/src/libeegdev.la
verifysplit_LDADD = $(top_builddir)/src/eegdev.lo\
                    $(top_builddir)/src/eegdev-types.lo\
                    $(top_builddir)/src/device-helper.lo $(CORE_LIBS)
verifycast_LDADD = $(top_builddir)/src/eegdev.lo\
		   $(top_builddir)/src/eegdev-types.lo $(CORE_LIBS)
syseegfile_LDADD = $(top_builddir)/src/libeegdev.la -lxdffileio
