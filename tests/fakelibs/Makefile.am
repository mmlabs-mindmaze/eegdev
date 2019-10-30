AM_CPPFLAGS = -I$(top_srcdir) $(CPPFLAGS)

check_LTLIBRARIES =

if GTEC_SUPPORT
check_LTLIBRARIES += libgusbampapi.la
endif

libgusbampapi_la_SOURCES = gusbamp.c fakegtec.h
libgusbampapi_la_LDFLAGS = -rpath /nowhere -no-undefined
libgusbampapi_la_LIBADD = $(LIB_MMLIB)

if ACT2_SUPPORT
check_LTLIBRARIES += libfakeact2.la
endif

libfakeact2_la_SOURCES = activetwo.c fakeact2.h
libfakeact2_la_LDFLAGS = -rpath /nowhere -no-undefined
libfakeact2_la_LIBADD = $(LIB_MMLIB)

if TIA_SUPPORT
check_LTLIBRARIES += libfaketia.la
endif

libfaketia_la_SOURCES = tia-server.h tia-server.c
libfaketia_la_LIBADD = $(LIB_MMLIB) $(TIA_LIBS)
