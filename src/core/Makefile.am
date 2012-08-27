AM_CPPFLAGS = -I$(top_srcdir)/lib \
              -DPKGLIBDIR='"$(pkglibdir)"' \
	      -DPKGSYSCONFDIR='"$(pkgsysconfdir)"'
AM_CFLAGS = $(CFLAG_VISIBILITY)

lib_LTLIBRARIES =
BUILT_SOURCES =
CLEANFILES =

EXTRA_DIST = conffile.l conffile.y
GENERATED = conffile.lex.c conffile.lex.h conffile.tab.c conffile.tab.h

%.lex.c %.lex.h: %.l
	$(AM_V_GEN)flex $^

%.tab.c %.tab.h: %.y
	$(AM_V_GEN)bison $^


libeegdev_la_SOURCES = eegdev.h eegdev-pluginapi.h core.c	\
		       coreinternals.h typecast.c device-helper.c	\
		       opendev.c sensortypes.c \
		       configuration.h configuration.c confparser.h
nodist_libeegdev_la_SOURCES = $(GENERATED)

libeegdev_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined			\
                       -version-info $(CURRENT):$(REVISION):$(AGE)
libeegdev_la_LIBADD = $(top_builddir)/lib/libgnu.la $(LIB_DLOPEN)

pkgconfigdir = $(libdir)/pkgconfig

if BUILD_CORELIB
BUILT_SOURCES += $(GENERATED)
CLEANFILES += $(GENERATED)
lib_LTLIBRARIES += libeegdev.la
include_HEADERS = eegdev.h eegdev-pluginapi.h
pkgconfig_DATA = eegdev.pc
endif

