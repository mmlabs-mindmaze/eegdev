
check_LTLIBRARIES =

if GTEC_SUPPORT
check_LTLIBRARIES += libfakegtec.la
endif

libfakegtec_la_SOURCES = time-utils.h gusbamp.c fakegtec.h
libfakegtec_la_LDFLAGS = -rpath /nowhere
libfakegtec_la_LIBADD = -lrt

if GTEC_CHILD
libfakegtec_la_CFLAGS = $(CFLAGS32)
libfakegtec_la_CPPFLAGS = $(CPPFLAGS32)
endif

