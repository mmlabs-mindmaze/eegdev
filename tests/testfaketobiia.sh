#!/bin/sh

prg=systobiia$EXEEXT

retval=0

if ! $prg -c 1
then
	retval=1
fi

exit $retval


