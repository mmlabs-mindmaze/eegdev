#!/bin/sh

set -e
set -x

echo "PATH=$PATH"

if ! eegdev_acq$EXEEXT -d saw > eegdev_acq.log
then
	exit 1
fi

rm eegdev_acq.log
exit 0
