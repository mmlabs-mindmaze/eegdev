/*
    Copyright (C) 2010-2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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
#include "configuration.h"
#include "../../lib/decl-dlfcn.h"

#define PLUGINS_DIR	PKGLIBDIR
const char default_confpath[] = PKGSYSCONFDIR;


struct conf {
	const char* confpath;
	struct egdi_config config[3];
};


static
int init_configuration(struct conf* cf, const char* str)
{
	unsigned int i;

	// Initialize configs
	for (i=0; i<3; i++)
		egdi_init_config(&cf->config[i]);

	// Set the search path for configuration file
	cf->confpath = getenv("EEGDEV_CONF_DIR");
	if (!cf->confpath)
		cf->confpath = default_confpath;

	// Load config files
	return egdi_parse_confline(&cf->config[2], str);
}


static
void free_configuration(struct conf* cf)
{
	unsigned int i;

	for (i=0; i<3; i++)
		egdi_free_config(&cf->config[i]);
}


static
int load_configuration_file(struct conf* cf, const char* file, int global)
{
	size_t pathlen = strlen(cf->confpath) + strlen(file) + 2;
	int index = global ? 0 : 1;
	char filepath[pathlen];

	sprintf(filepath, "%s/%s", cf->confpath, file);
	egdi_reinit_config(&cf->config[index]);
	return egdi_parse_conffile(&cf->config[index], filepath);
}


static
const char* get_conf_setting(struct conf* cf,
                             const char* name, const char* defvalue)
{
	int i;
	const char* val;

	for (i=2; i>=0; i--) {
		val = egdi_get_setting_value(&cf->config[i], name);
		if (val)
			return val;
	}

	return defvalue;
}



/**************************************************************************
 *                         Table of known devices                         *
 **************************************************************************/
static
const char* prefered_devices[] = {"biosemi", "gtec", "datafile", NULL};

/**************************************************************************
 *                           Implementation                               *
 **************************************************************************/
LOCAL_FN
const struct egdi_chinfo* egdi_get_conf_mapping(struct devmodule* mdev,
                                                const char* name, int* pnch)
{
	int i;
	struct egdi_chinfo *chmap;
	struct conf* cf = get_eegdev(mdev)->cf;

	if (!cf || !pnch)
		return NULL;

	for (i=2; i>=0; i--) {
		chmap = egdi_get_cfmapping(&cf->config[i], name, pnch);
		if (chmap)
			return chmap;
	}

	return NULL;
}


static
struct eegdev* open_init_device(const struct egdi_plugin_info* info,
                                unsigned int nopt, struct conf* cf)
{
	struct eegdev* dev;
	unsigned int i;
	const char* optval[nopt+1], *name, *defvalue;

	// Get options values
	for (i=0; i<nopt; i++) {
		name = info->supported_opts[i].name;
		defvalue = info->supported_opts[i].defvalue;
		optval[i] = get_conf_setting(cf, name, defvalue);
	}
	optval[nopt] = NULL;
	
	// Create and initialize the base structure
	if (!(dev = egdi_create_eegdev(info)))
		return NULL;
	dev->cf = cf;

	// then try to execute the device specific initialization
	if (info->open_device(&dev->module, optval)) {
		egd_destroy_eegdev(dev);
		return NULL;
	}

	dev->cf = NULL;
	return dev;
}


static
struct eegdev* open_plugin_device(const char* dname, struct conf *cf)
{
	struct eegdev* dev = NULL;
	void *handle;
	const struct egdi_plugin_info* info;
	const char* dir = getenv("EEGDEV_PLUGINS_DIR");
	char path[128], confname[64];
	unsigned int nopt;

	// dlopen the plugin
	snprintf(path, sizeof(path),
	         "%s/%s"LT_MODULE_EXT, (dir?dir:PLUGINS_DIR), dname);
	if ( !(handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL))
	  || !(info = dlsym(handle, "eegdev_plugin_info"))
	  || (info->plugin_abi != EEGDEV_PLUGIN_ABI_VERSION) ) {
	  	errno = ENOSYS;
		goto fail;
	}

	// Count the number of options supported by the plugin
	nopt = 0;
	if (info->supported_opts) {
		while (info->supported_opts[nopt].name)
			nopt++;
	}

	// Load device specific configuration
	snprintf(confname, sizeof(confname), "%s.conf", dname);
	if (load_configuration_file(cf, confname, 0))
		goto fail;

	// Try to open the device
	dev = open_init_device(info, nopt, cf);
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
struct eegdev* open_any(struct conf* cf)
{
	int i;
	struct eegdev* dev = NULL;

	for (i=0; prefered_devices[i] != NULL; i++) {
		dev = open_plugin_device(prefered_devices[i], cf);
		if (dev != NULL)
			break;
	}

	if (dev == NULL)
		errno = ENODEV;
	return dev;
}


API_EXPORTED
struct eegdev* egd_open(const char* confstring)
{
	struct conf cf;
	struct eegdev* dev = NULL;
	const char* device;

	// Load global configuration
	if (init_configuration(&cf, confstring)
	 || load_configuration_file(&cf, "eegdev.conf", 1)) {
	 	free_configuration(&cf);
		return NULL;
	}

	// Get device type
	device = get_conf_setting(&cf, "device", "any");
	
	if (!strcmp(device, "any"))
		dev = open_any(&cf);
	else
		dev = open_plugin_device(device, &cf);

	free_configuration(&cf);
	return dev;
}

