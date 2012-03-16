
AM_CPPFLAGS = -I$(top_srcdir) -I$(top_builddir)/lib -I$(top_srcdir)/lib $(CPPFLAGS)

check_LTLIBRARIES =

if GTEC_SUPPORT
check_LTLIBRARIES += libfakegtec.la
endif

libfakegtec_la_SOURCES = time-utils.h gusbamp.c fakegtec.h
libfakegtec_la_LDFLAGS = -rpath /nowhere -no-undefined
libfakegtec_la_LIBADD = $(top_builddir)/lib/libgnu.la $(LIB_CLOCK_FUNCS)

if ACT2_SUPPORT
check_LTLIBRARIES += libfakeact2.la
endif

libfakeact2_la_SOURCES = time-utils.h activetwo.c fakeact2.h
libfakeact2_la_LDFLAGS = -rpath /nowhere -no-undefined
libfakeact2_la_LIBADD = $(top_builddir)/lib/libgnu.la $(LIB_CLOCK_FUNCS)

if TIA_SUPPORT
check_LTLIBRARIES += libfaketia.la
endif

libfaketia_la_SOURCES = time-utils.h tia-server.h tia-server.c
libfaketia_la_LIBADD = $(top_builddir)/lib/libgnu.la $(LIB_CLOCK_FUNCS) $(TIA_LIBS) $(LIBSOCKET)
