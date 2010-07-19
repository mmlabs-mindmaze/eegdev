AM_CFLAGS = -I$(top_srcdir)/src

check_PROGRAMS = sysbiosemi syseegfile

sysbiosemi_SOURCES = sysbiosemi.c
sysbiosemi_LDADD = $(top_builddir)/src/libeegdev.la
syseegfile_SOURCES = syseegfile.c
syseegfile_LDADD = $(top_builddir)/src/libeegdev.la

TESTS = sysbiosemi syseegfile
