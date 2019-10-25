SUBDIRS = fakelibs

EXTRA_DIST=verify-cast.sh\
           testfakegtec.sh testfakeact2.sh testfaketobiia.sh \
	   conffiles/eegdev.conf conffiles/gtec.conf \
	   conffiles/datafile.conf conffiles/biosemi.conf

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/core \
              $(CPPFLAGS)


TESTS_ENVIRONMENT = PATH=$(PATH):$(builddir) \
                  EEGDEV_PLUGINS_DIR=$(abs_top_builddir)/src/plugins/.libs \
                  EEGDEV_CONF_DIR=$(srcdir)/conffiles \
                  DLOPEN_GUSBAMP=$(DLOPEN_GUSBAMP) \
                  EXEEXT=$(EXEEXT) builddir=$(builddir)


check_PROGRAMS = verifycast verifysplit
TESTS = verify-cast.sh verifysplit

if XDF_SUPPORT
check_PROGRAMS += syseegfile
TESTS += syseegfile
endif

# it it not possible to run those tests under windows
if !OS_TYPE_WIN32

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

endif  # OS_TYPE_WIN32

if BUILD_CORELIB
LDADD = $(top_builddir)/src/core/libeegdev.la
else
LDADD = $(CORE_EEGDEV_LIBS)
endif

verifysplit_LDADD = $(top_builddir)/src/core/core.lo\
                    $(top_builddir)/src/core/typecast.lo\
                    $(top_builddir)/src/core/sensortypes.lo\
                    $(top_builddir)/src/core/device-helper.lo\
		    		$(LIB_MMLIB)
verifycast_LDADD = $(top_builddir)/src/core/core.lo\
		   $(top_builddir)/src/core/typecast.lo\
                   $(top_builddir)/src/core/sensortypes.lo\
                   $(top_builddir)/src/core/device-helper.lo\
					$(LIB_MMLIB)
syseegfile_LDADD = $(LDADD) -lxdffileio
systobiia_LDADD = $(LDADD) $(builddir)/fakelibs/libfaketia.la
