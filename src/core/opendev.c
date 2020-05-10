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

#include <mmdlfcn.h>
#include <mmerrno.h>
#include <mmlib.h>
#include <stdio.h>
#include <string.h>

#include "configuration.h"
#include "coreinternals.h"

#define PLUGINS_DIR	PKGLIBDIR
const char default_confpath[] = PKGSYSCONFDIR;


struct conf {
	char confpath[256];
	char home_confpath[256];
	struct egdi_config config[3];
};


static
int init_configuration(struct conf* cf, const char* str)
{
	unsigned int i;

	// Initialize configs
	for (i=0; i<3; i++)
		egdi_init_config(&cf->config[i]);

	// Set the search path for global configuration files
	strncpy(cf->confpath, mm_getenv("EEGDEV_CONF_DIR", default_confpath),
		sizeof(cf->confpath)-1);

	// Set the search path for home configuration files
	snprintf(cf->home_confpath, sizeof(cf->home_confpath)-1, "%s/%s",
		mm_get_basedir(MM_CONFIG_HOME), PACKAGE_NAME);

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
	size_t homepathlen = strlen(cf->home_confpath) + strlen(file) + 2;
	int index = global ? 0 : 1;
	char filepath[pathlen];
	char home_filepath[homepathlen];

	sprintf(filepath, "%s/%s", cf->confpath, file);
	sprintf(home_filepath, "%s/%s", cf->home_confpath, file);

	egdi_reinit_config(&cf->config[index]);

	if ( egdi_parse_conffile(&cf->config[index], filepath)
	  || egdi_parse_conffile(&cf->config[index], home_filepath) )
		return -1;

	return 0;
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

	if (!cf || !name || !pnch)
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
void* load_compat_plugin(const char* plugin_path,
                         const struct egdi_plugin_info** pinfo)
{
	const struct egdi_plugin_info* info;
	void* handle;

	if ( !plugin_path
	  || !(handle = mm_dlopen(plugin_path, MM_LD_LAZY))
	  || !(info = mm_dlsym(handle, "eegdev_plugin_info"))
	  || (info->plugin_abi != EEGDEV_PLUGIN_ABI_VERSION) ) {
		if (handle)
			mm_dlclose(handle);
		errno = ENOSYS;
		return NULL;
	}

	*pinfo = info;
	return handle;
}


static
struct eegdev* open_plugin_device(const char* dname, struct conf *cf)
{
	struct eegdev* dev = NULL;
	void *handle;
	const struct egdi_plugin_info* info;
	char path[128], aux_path[128], confname[64];
	const char* auxdir;
	unsigned int nopt;

	// Set the plugin paths
	snprintf(path, sizeof(path),"%s/%s"LT_MODULE_EXT, 
	         mm_getenv("EEGDEV_PLUGINS_DIR", PLUGINS_DIR), dname);

	auxdir = get_conf_setting(cf, "aux_plugindir", NULL);
	if (auxdir)
		snprintf(aux_path, sizeof(aux_path),
		         "%s/%s"LT_MODULE_EXT, auxdir, dname);

	// mm_dlopen the plugin
	if ( !(handle = load_compat_plugin(path, &info))
	  && !(handle = load_compat_plugin(aux_path, &info)) ) {
		return NULL;
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
		mm_dlclose(handle);
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


/**
 * egd_open() - opens an EEG device
 * @confstring: description of the EEG device to open 
 *
 * egd_open() opens a EEG device according to the @confstring
 * description. If @confstring is NULL, the first device supported by the
 * library will be opened.
 *
 * The @confstring argument specifies the type of EEG device required to be
 * opened and the parameters which the system should be configured
 * with (overriding defaults settings). The syntax of the string is defined in
 * eegdev-open-options().
 *
 * The syntax may change in future releases and programs should not rely on it
 * to configure the device. It is provided only to users of the programs to
 * select the EEG acquisition device and configure it. Programs using the
 * eegdev library should pass the string provided by the users untouched.
 *
 * @confstring is not the only way to provides configuration information
 * about the device to open. The configuration is read from the configuration
 * files. The final values of the settings provided to the opening function are
 * defined (and overridden) in the following order:
 * 
 * - default values
 *
 * - shared configuration file
 *
 * - device specific configuration file
 *
 * - @confstring
 *
 * This order indicates that a setting value specified in @confstring
 * will always override any setting value defined by other mean.
 *
 * Return:
 * The function returns a pointer to the opened EEG device in case of success.
 * Otherwise NULL is returned errno is set accordingly.
 *
 * Errors:
 * ENOSYS
 *   the device part of @confstring does not refer to a device supported by
 *   any of the installed eegdev plugin modules.
 *
 * EINVAL
 *   one of the option specified in @confstring is unknown.
 *
 * ENODEV
 *   The specified device is not connected.
 *
 * EBUSY
 *   The specified device is already in use.
 *
 * ECHILD
 *   The specified device needs an auxiliary child process whose executable
 *   file cannot be found.
 *
 * 
 * Environment:
 * "EEGDEV_PLUGINS_DIR" 4
 *   This variable controls which folder should be search to find plugin
 *   modules. If unset, they will be searched in the subfolder
 *   @PACKAGE_NAME@ of the installation folder of the libraries.
 *
 * "EEGDEV_CONF_DIR" 4
 *   This variable controls which folder should be search to find the
 *   configuration files. If unset, they will be searched in
 *   @SYSCONFDIR@/@PACKAGE_NAME@.
 */
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

