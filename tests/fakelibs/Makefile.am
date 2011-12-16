
AM_CPPFLAGS = -I$(top_srcdir)/lib $(CPPFLAGS)

check_LTLIBRARIES =

if GTEC_SUPPORT
check_LTLIBRARIES += libfakegtec.la
endif

libfakegtec_la_SOURCES = time-utils.h gusbamp.c fakegtec.h
libfakegtec_la_LDFLAGS = -rpath /nowhere -no-undefined
libfakegtec_la_LIBADD = $(top_builddir)/lib/libportability.la $(CLOCK_LIBS)

if ACT2_SUPPORT
check_LTLIBRARIES += libfakeact2.la
endif

libfakeact2_la_SOURCES = time-utils.h activetwo.c fakeact2.h
libfakeact2_la_LDFLAGS = -rpath /nowhere -no-undefined
libfakeact2_la_LIBADD = $(top_builddir)/lib/libportability.la $(CLOCK_LIBS)
