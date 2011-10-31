SUBDIRS = fakelibs

EXTRA_DIST=verify-cast.sh testfakegtec.sh
AM_CPPFLAGS =  -I$(top_srcdir)/src/core -I$(top_srcdir) $(CPPFLAGS)

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

LDADD = $(top_builddir)/src/core/libeegdev.la
verifysplit_LDADD = $(top_builddir)/src/core/core.lo\
                    $(top_builddir)/src/core/typecast.lo\
                    $(top_builddir)/src/plugins/device-helper.lo\
		    $(LTLIBOBJS) $(CORE_LIBS)
verifycast_LDADD = $(top_builddir)/src/core/core.lo\
		   $(top_builddir)/src/core/typecast.lo\
		   $(LTLIBOBJS) $(CORE_LIBS)
syseegfile_LDADD = $(top_builddir)/src/core/libeegdev.la -lxdffileio
