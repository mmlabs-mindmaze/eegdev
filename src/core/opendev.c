/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "coreinternals.h"
#include "../../lib/decl-dlfcn.h"

#define PLUGINS_DIR LIBDIR"/"PACKAGE_NAME

/**************************************************************************
 *                         Table of known devices                         *
 **************************************************************************/
static
const char* prefered_devices[] = {"biosemi", "gtec", "datafile", NULL};

/**************************************************************************
 *                           Implementation                               *
 **************************************************************************/
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
struct eegdev* open_init_device(const struct egdi_plugin_info* info,
                                const char* optv[])
{
	struct eegdev* dev;
	
	// Create and initialize the base structure
	// then try to execute the device specific initialization
	if ( !(dev = egdi_create_eegdev(info))
	   || info->open_device(dev, optv)) {
		egd_destroy_eegdev(dev);
		return NULL;
	}

	egd_update_capabilities(dev);
	return dev;
}


static
struct eegdev* open_plugin_device(const char* dname, const char* optv[])
{
	struct eegdev* dev = NULL;
	void *handle;
	const struct egdi_plugin_info* info;
	const char* dir = getenv("EEGDEV_PLUGINS_DIR");
	char path[128];

	// dlopen the plugin
	sprintf(path, "%s/%s"LT_MODULE_EXT, (dir?dir:PLUGINS_DIR), dname);
	if ( !(handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL))
	  || !(info = dlsym(handle, "eegdev_plugin_info"))
	  || (info->plugin_abi != EEGDEV_PLUGIN_ABI_VERSION) ) {
	  	errno = ENOSYS;
		goto fail;
	}

	// Try to open the device
	dev = open_init_device(info, optv);
	if (!dev) 
		goto fail;
		
	dev->handle = handle;
	return dev;

fail:
	if (handle)
		dlclose(handle);
	return NULL;
}


static
struct eegdev* open_any(const char* optv[])
{
	int i;
	struct eegdev* dev = NULL;

	for (i=0; prefered_devices[i] != NULL; i++) {
		dev = open_plugin_device(prefered_devices[i], optv);
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
	
	optv = parse_device_options(currpoint);
	if (!strcmp(device, "any"))
		dev = open_any(optv);
	else
		dev = open_plugin_device(device, optv);

	free(optv);
	free(workcopy);
	return dev;
}

