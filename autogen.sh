#!/bin/sh -e

cd `dirname $0`

if ! gnulib-tool --update > gnulib.log ; then
	cat gnulib.log
	rm gnulib.log
	exit 1
fi
rm gnulib.log

autoreconf -i

