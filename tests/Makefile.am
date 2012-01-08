SUBDIRS = fakelibs

EXTRA_DIST=verify-cast.sh\
           testfakegtec.sh testfakeact2.sh testfaketobiia.sh
AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/core -I$(top_srcdir)/lib $(CPPFLAGS)

TESTS_ENVIRONMENT = PATH=$(PATH):$(builddir) \
                    EEGDEV_PLUGINS_DIR=$(abs_top_builddir)/src/plugins/.libs \
		    EXEEXT=$(EXEEXT) builddir=$(builddir)

check_PROGRAMS = verifycast verifysplit
TESTS = verify-cast.sh verifysplit

if XDF_SUPPORT
check_PROGRAMS += syseegfile
TESTS += syseegfile
endif
if ACT2_SUPPORT
check_PROGRAMS += sysbiosemi 
TESTS += testfakeact2.sh 
endif
if GTEC_SUPPORT
check_PROGRAMS += sysgtec
TESTS += testfakegtec.sh
endif
if TIA_SUPPORT
check_PROGRAMS += systobiia
TESTS += testfaketobiia.sh
endif
if NSKY_SUPPORT
check_PROGRAMS += sysneurosky
TESTS += sysneurosky
endif

if BUILD_CORELIB
LDADD = $(top_builddir)/src/core/libeegdev.la
else
LDADD = $(CORE_EEGDEV_LIBS)
endif

verifysplit_LDADD = $(top_builddir)/src/core/core.lo\
                    $(top_builddir)/src/core/typecast.lo\
                    $(top_builddir)/src/plugins/device-helper.lo\
		    $(top_builddir)/lib/libportability.la $(DLOPEN_LIBS)
verifycast_LDADD = $(top_builddir)/src/core/core.lo\
		   $(top_builddir)/src/core/typecast.lo\
		   $(top_builddir)/lib/libportability.la $(DLOPEN_LIBS)
syseegfile_LDADD = $(LDADD) -lxdffileio
systobiia_LDADD = $(LDADD) $(builddir)/fakelibs/libfaketia.la
