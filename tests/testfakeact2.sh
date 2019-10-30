#!/bin/sh

prg=sysbiosemi$EXEEXT

retval=0

if ! $prg -d 0 -c 1
then
	retval=1
fi

if ! $prg -d 1 -c 1
then
	retval=1
fi

exit $retval

