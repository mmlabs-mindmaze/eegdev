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

static struct eegdev* open_any(const char* optv[]);

/**************************************************************************
 *                         Table of known devices                         *
 **************************************************************************/
struct devreg {
	const char* desc_string;
	eegdev_open_proc open_fn;
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
const char** parse_device_options(char* optstr)
{
	int i, nval = 0, nmax = 32;
	const char** optv = malloc(nmax*sizeof(*optv));
	const char *option, *optval;
	
	while (optstr) {
		// Get next option name
		option = optstr;
		optstr = strchr(optstr, '|');
		if (!optstr)
			break;
		*optstr++ = '\0';

		// Null terminate previous field and get option value
		optval = optstr;
		optstr = strchr(optstr, '|');
		if (optstr)
			*optstr++ = '\0';

		// Search if the same option has not been already set
		for (i=0; i<nval; i+=2)
			if (!strcmp(optv[i], option))
				break;
		optv[i++] = option;
		optv[i++] = optval;

		// Increase the size of the array if necessary
		nval = i > nval ? i : nval;
		if (nval >= nmax-2)
			optv = realloc(optv, (nmax+=32)*sizeof(*optv));
	}
	
	optv[nval] = NULL;
	return optv;
}


static
struct eegdev* open_any(const char* optv[])
{
	int i, devid;
	struct eegdev* dev = NULL;

	for (i=0; prefered_devices[i] != NULL; i++) {
		devid = parse_device_type(prefered_devices[i]);
		if (devid < 0)
			continue;
		dev = supported_device[devid].open_fn(optv);
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
	char *workcopy, *currpoint, *device;
	const char** optv = NULL;
	struct eegdev* dev = NULL;
	int dev_type;

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
	
	optv = parse_device_options(currpoint);

	// Open device
	dev = supported_device[dev_type].open_fn(optv);

exit:
	free(optv);
	free(workcopy);
	return dev;
}

