/*
    Copyright (C) 2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "eegdev-common.h"
#include "eegdev.h"
#include "devices.h"

static struct eegdev* open_any(const struct opendev_options*);

/**************************************************************************
 *                         Table of known devices                         *
 **************************************************************************/
struct devreg {
	const char* desc_string;
	struct eegdev* (*open_fn)(const struct opendev_options*);
};

#define DECLARE_DEVICE(NAME)  { #NAME, open_##NAME }
static
struct devreg supported_device[] = {
#ifdef ACT2_SUPPORT
	DECLARE_DEVICE(biosemi),
#endif
#ifdef GTEC_SUPPORT
	DECLARE_DEVICE(gtec),
#endif
#ifdef NSKY_SUPPORT
	DECLARE_DEVICE(neurosky),
#endif
#ifdef XDF_SUPPORT
	DECLARE_DEVICE(datafile),
#endif
	DECLARE_DEVICE(any)
};
#define NUM_DEVICE   (sizeof(supported_device)/sizeof(supported_device[0]))
static
const char* prefered_devices[] = {"biosemi", "gtec", "datafile", NULL};

/**************************************************************************
 *                           Implementation                               *
 **************************************************************************/
static
int parse_device_type(const char* device)
{
	int i;

	for (i=0; i<(int)NUM_DEVICE; i++) {
		if (strcmp(supported_device[i].desc_string, device) == 0)
			return i;
	}

	return -1;
}


static
int parse_device_option(struct opendev_options* devopt,
                        const char* name, const char* val)
{
	if (strcmp(name, "numch") == 0)
		devopt->numch = atoi(val);
	else if (strcmp(name, "path") == 0)
		devopt->path = val;
	else {
		errno = EINVAL;
		return -1;
	}
	return 0;
}


static
struct eegdev* open_any(const struct opendev_options* opt)
{
	int i, devid;
	struct eegdev* dev = NULL;

	for (i=0; prefered_devices[i] != NULL; i++) {
		devid = parse_device_type(prefered_devices[i]);
		if (devid < 0)
			continue;
		dev = supported_device[devid].open_fn(opt);
		if (dev != NULL)
			break;
	}

	if (dev == NULL)
		errno = ENODEV;
	return dev;
}


API_EXPORTED
struct eegdev* egd_open(const char* conf)
{
	char *workcopy, *currpoint, *device, *option, *optval;
	struct eegdev* dev = NULL;
	int dev_type;
	struct opendev_options opt = {.numch = -1, .path = NULL};

	if (conf == NULL)
		conf = "any";
	currpoint = workcopy = strdup(conf);
	if (currpoint == NULL)
		return NULL;

	// Get device type
	device = currpoint;
	currpoint = strchr(currpoint, '|');
	if (currpoint)
		*currpoint++ = '\0';
	dev_type = parse_device_type(device);
	if (dev_type < 0) {
		errno = ENOSYS;
		goto exit;
	}
	
	// Parse options
	while (currpoint) {
		// Get next option name
		option = currpoint;
		currpoint = strchr(currpoint, '|');
		if (!currpoint)
			break;
		*currpoint++ = '\0';

		// Null terminate previous field and get option value
		optval = currpoint;
		currpoint = strchr(currpoint, '|');
		if (currpoint)
			*currpoint++ = '\0';

		// TODO: Use options
		if (parse_device_option(&opt, option, optval))
			goto exit;
	}

	// Open device
	dev = supported_device[dev_type].open_fn(&opt);

exit:
	free(workcopy);
	return dev;
}

