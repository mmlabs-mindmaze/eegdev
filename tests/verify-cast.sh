#!/bin/sh

if [ -f verifycast ] ; then
	prog=verifycast
elif [ -f verifycast.exe ] ; then
	prog=verifycast.exe
else
	echo "verifycast program not found" 1>&2
	exit 1;
fi

if ! $prog -s 64 -S 64 -c 64
then
	echo "\tcast function fails when sample size are equal"
	retval=1
fi

if ! $prog -s 64 -S 72 -c 64
then
	echo "\tcast function fails when sample size are different and"
	echo "\t\tchunksize on boundary of ringbuffer sample size"
	retval=1
fi

if ! $prog -s 64 -S 72 -c 72
then
	echo "\tcast function fails when sample size are different and "
	echo "\t\tchunksize on boundary of input buffer sample size"
	retval=1
fi
if ! $prog -s 64 -S 72 -c 50
then
	echo "\tcast function fails when sample size are different and "
	echo "\t\tchunksize not falling on sample boundary"
	retval=1
fi
if ! $prog -s 64 -S 72 -c 64 -o 2
then
	echo "\tcast function fails when sample size are different and"
	echo "\t\tchunksize on boundary of ringbuffer sample size and"
	echo "\t\toffset in input buffer"
	retval=1
fi

if ! $prog -s 64 -S 72 -c 72 -o 2
then
	echo "\tcast function fails when sample size are different and "
	echo "\t\tchunksize on boundary of input buffer sample size"
	echo "\t\toffset in input buffer"
	retval=1
fi

exit $retval
