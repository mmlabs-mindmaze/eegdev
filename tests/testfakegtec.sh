#!/bin/sh

LD_PRELOAD=$builddir/fakelibs/.libs/libfakegtec.so testfakegtec$EXEEXT
