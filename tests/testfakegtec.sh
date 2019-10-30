#!/bin/sh

prg=sysgtec$EXEEXT

retval=0

if ! $prg -n 1 -d 0 -c 1
then
	retval=1
fi

if ! $prg -n 1 -d 1 -c 1
then
	retval=1
fi

if ! $prg -n 2 -d 0 -c 1
then
	retval=1
fi

if ! $prg -n 2 -d 1 -c 1
then
	retval=1
fi

exit $retval
