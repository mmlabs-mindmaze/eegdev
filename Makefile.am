eol=

ACLOCAL_AMFLAGS = -I m4 --install
SUBDIRS = gnulib-local lib doc/examples tests/fakelibs

EXTRA_DIST = autogen.sh m4/gnulib-cache.m4 README_build

lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
man_MANS =
nodist_man_MANS =
check_PROGRAMS =
TESTS =
BUILT_SOURCES =
CLEANFILES =

AM_CPPFLAGS = -I$(srcdir) -I$(srcdir)/lib -I$(builddir)/lib \
              -I$(srcdir)/src/core \
              -DPKGLIBDIR='"$(pkglibdir)"' \
	      -DPKGSYSCONFDIR='"$(pkgsysconfdir)"'
AM_CFLAGS = $(CFLAG_VISIBILITY)

GNULIB = $(builddir)/lib/libgnu.la

###########################################################################
#
#      Core eegdev library
#
###########################################################################
EXTRA_DIST += src/core/configuration.l src/core/configuration.y
GENERATED = src/core/configuration.lex.c src/core/configuration.lex.h \
            src/core/configuration.tab.c src/core/configuration.tab.h
CLEANFILES += $(GENERATED)

%.lex.c %.lex.h: %.l
	$(AM_V_GEN)flex -o $@ --header-file=$(patsubst %.lex.c,%.lex.h,$@) $^

%.tab.c %.tab.h: %.y
	$(AM_V_GEN)bison -o $@ $^


libeegdev_la_SOURCES = \
	src/core/eegdev.h \
	src/core/eegdev-pluginapi.h \
	src/core/core.c	\
	src/core/coreinternals.h \
	src/core/typecast.c \
	src/core/device-helper.c	\
	src/core/opendev.c \
	src/core/sensortypes.c \
	src/core/configuration.h \
	src/core/confparser.h \
	$(eol)

nodist_libeegdev_la_SOURCES = $(GENERATED)

libeegdev_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined			\
                       -version-info $(CURRENT):$(REVISION):$(AGE)
libeegdev_la_LIBADD = $(GNULIB) $(LIB_DLOPEN)

pkgconfigdir = $(libdir)/pkgconfig

if BUILD_CORELIB
BUILT_SOURCES += $(GENERATED)
lib_LTLIBRARIES += libeegdev.la
include_HEADERS = \
	src/core/eegdev.h \
	src/core/eegdev-pluginapi.h \
	$(eol)
pkgconfig_DATA = src/core/eegdev.pc

nodist_man_MANS += doc/egd_open.3
man_MANS += \
	doc/egd_get_data.3 \
	doc/egd_get_available.3	\
	doc/egd_acq_setup.3 \
        doc/egd_start.3 \
	doc/egd_stop.3 \
	doc/egd_close.3 \
	doc/egd_get_cap.3 \
	doc/egd_get_numch.3 \
	doc/egd_channel_info.3 \
	doc/egd_sensor_name.3 \
	doc/egd_sensor_type.3 \
	doc/eegdev-open-options.5 \
	$(eol)

endif


###########################################################################
#
#      eegdev plugins
#
###########################################################################
plugin_LDFLAGS=-module -avoid-version -no-undefined

if ACT2_SUPPORT
pkglib_LTLIBRARIES += biosemi.la
man_MANS += doc/eegdev-biosemi.5
endif
biosemi_la_SOURCES = src/plugins/biosemi.c
biosemi_la_LIBADD = $(ACT2_LIBS) $(GNULIB)
biosemi_la_LDFLAGS = $(plugin_LDFLAGS)

if XDF_SUPPORT
pkglib_LTLIBRARIES += datafile.la
man_MANS += doc/eegdev-datafile.5
endif
datafile_la_SOURCES = src/plugins/fileout.c
datafile_la_LIBADD = $(XDF_LIBS) $(GNULIB) $(LIB_CLOCK_FUNCS) $(LTLIBINTL)
datafile_la_LDFLAGS = $(plugin_LDFLAGS)

if NSKY_SUPPORT
pkglib_LTLIBRARIES += neurosky.la
man_MANS += doc/eegdev-neurosky.5
endif
neurosky_la_SOURCES = src/plugins/neurosky.c
neurosky_la_LIBADD = $(NSKY_LIBS) $(GNULIB)
neurosky_la_LDFLAGS = $(plugin_LDFLAGS)

if GTEC_SUPPORT
pkglib_LTLIBRARIES += gtec.la
man_MANS += doc/eegdev-gtec.5
endif
gtec_la_SOURCES = src/plugins/gtec.c
gtec_la_LIBADD = $(GNULIB) $(LIB_CLOCK_FUNCS)
gtec_la_LDFLAGS = $(plugin_LDFLAGS)
if DLOPEN_GUSBAMP
gtec_la_SOURCES += \
	src/plugins/gusbamp-types.h \
	src/plugins/gusbamp-loader.h \
	src/plugins/gusbamp-loader.c \
	$(eol)
gtec_la_LIBADD += $(LIB_DLOPEN)
else
gtec_la_LIBADD += $(GTEC_LIBS)
endif


if TIA_SUPPORT
pkglib_LTLIBRARIES += tobiia.la
man_MANS += doc/eegdev-tobiia.5
endif
tobiia_la_SOURCES = src/plugins/tobiia.c
tobiia_la_LIBADD = $(TIA_LIBS) $(GNULIB) $(LIBSOCKET) $(GETADDRINFO_LIB)
tobiia_la_LDFLAGS = $(plugin_LDFLAGS)


###########################################################################
#
#      tests
#
###########################################################################
EXTRA_DIST +=
	tests/verify-cast.sh \
        tests/testfakegtec.sh \
	tests/testfakeact2.sh \
	tests/testfaketobiia.sh \
	tests/conffiles/eegdev.conf \
	tests/conffiles/gtec.conf \
	tests/conffiles/datafile.conf \
	tests/conffiles/biosemi.conf \
	$(eol)


TESTS_ENVIRONMENT = PATH=$(PATH):$(builddir) \
                  EEGDEV_PLUGINS_DIR=.libs \
                  EEGDEV_CONF_DIR=$(srcdir)/tests/conffiles \
                  DLOPEN_GUSBAMP=$(DLOPEN_GUSBAMP) \
                  EXEEXT=$(EXEEXT) builddir=$(builddir)


check_PROGRAMS += verifycast verifysplit
TESTS += tests/verify-cast.sh verifysplit

if XDF_SUPPORT
check_PROGRAMS += syseegfile
TESTS += syseegfile
endif
if ACT2_SUPPORT
check_PROGRAMS += sysbiosemi
TESTS += tests/testfakeact2.sh
endif
if GTEC_SUPPORT
check_PROGRAMS += sysgtec
TESTS += tests/testfakegtec.sh
endif
if TIA_SUPPORT
check_PROGRAMS += systobiia
TESTS += tests/testfaketobiia.sh
endif
if NSKY_SUPPORT
check_PROGRAMS += sysneurosky
TESTS += sysneurosky
endif

if BUILD_CORELIB
tests_LDADD = libeegdev.la
else
tests_LDADD = $(CORE_EEGDEV_LIBS)
endif

verifysplit_SOURCES = tests/verifysplit.c
verifysplit_LDADD = $(top_builddir)/src/core/core.lo\
                    $(top_builddir)/src/core/typecast.lo\
                    $(top_builddir)/src/core/sensortypes.lo\
                    $(top_builddir)/src/core/device-helper.lo\
		    $(top_builddir)/lib/libgnu.la $(LIB_DLOPEN)

verifycast_SOURCES = tests/verifycast.c
verifycast_LDADD = $(top_builddir)/src/core/core.lo\
		   $(top_builddir)/src/core/typecast.lo\
                   $(top_builddir)/src/core/sensortypes.lo\
                   $(top_builddir)/src/core/device-helper.lo\
		   $(top_builddir)/lib/libgnu.la $(LIB_DLOPEN)

sysneurosky_SOURCES = tests/sysneurosky.c
sysneurosky_LDADD = $(tests_LDADD)

sysgtec_SOURCES = tests/sysgtec.c
sysgtec_LDADD = $(tests_LDADD)

sysbiosemi_SOURCES = tests/sysbiosemi.c
sysbiosemi_LDADD = $(tests_LDADD)

syseegfile_SOURCES = tests/syseegfile.c
syseegfile_LDADD = $(tests_LDADD) -lxdffileio

systobiia_SOURCES = tests/systobiia.c
systobiia_LDADD = $(tests_LDADD) $(builddir)/tests/fakelibs/libfaketia.la

###########################################################################
#
# udev config  install/uninstall rules
#
###########################################################################
install-usb-plugdev:
	@echo "Installing 50-usb-plugdev.rules into /etc/udev/rules.d/ ..." \
	&& $(INSTALL) $(top_srcdir)/auxconfig/50-usb-plugdev.rules /etc/udev/rules.d \
	&& echo "done."

uninstall-usb-plugdev:
	@echo "Removing 50-usb-plugdev.rules from /etc/udev/rules.d/ ..." \
	&& rm -f /etc/udev/rules.d/50-usb-plugdev.rules && echo "done."
