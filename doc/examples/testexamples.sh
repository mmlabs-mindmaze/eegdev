#!/bin/sh

set -e
set -x

export PATH="$EEGDEV_PLUGINS_DIR:$CORELIB_PATH:$PATH"

if ! $(dirname $0)/eegdev_acq$EXEEXT -d saw > eegdev_acq.log
then
	exit 1
fi

rm eegdev_acq.log
exit 0
