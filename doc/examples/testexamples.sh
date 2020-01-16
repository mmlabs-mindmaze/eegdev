#!/bin/sh

set -x

export PATH="$CORELIB_PATH:$PATH"

if ! $EEGDEV_PLUGINS_DIR/eegdev_acq$EXEEXT -d saw > eegdev_acq.log
then
	exit 1
fi

rm eegdev_acq.log
exit 0
