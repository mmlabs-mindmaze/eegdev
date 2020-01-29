/*
    Copyright (C) 2013 Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

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
#include <mmsysio.h>
#include <mmthread.h>
#include <stdio.h>

#include "gusbamp-loader.h"

#define LOAD_FUNC_POINTER(module, name) \
	(name = (typeof(name)) mm_dlsym(module, #name))

static
const char* const gusbamp_name[] = {
	"libgusbampapi.so",
	"libgusbampapi.so.1.11",
	"libgusbampapi.so.1.10",
	"libgUSBAmpAPIso.so",
	"libgUSBAmpAPIso.so.1.11",
	"libgUSBAmpAPIso.so.1.10",
	"libgUSBAmpAPIa.so",
	"libgUSBAmpAPIa.so.1.11",
	"libgUSBAmpAPIa.so.1.11",
};

#define NUM_NAMES (sizeof(gusbamp_name)/sizeof(gusbamp_name[0]))
static void* gusbamp_module;
static int gusbamp_numref = 0;
static mm_thr_mutex_t lock = MM_THR_MTX_INITIALIZER;

static
int load_func_pointers(void* module)
{
	if ( !LOAD_FUNC_POINTER(module, GT_ShowDebugInformation)
	  || !LOAD_FUNC_POINTER(module, GT_UpdateDevices)
	  || !LOAD_FUNC_POINTER(module, GT_GetDeviceListSize)
	  || !LOAD_FUNC_POINTER(module, GT_GetDeviceList)
	  || !LOAD_FUNC_POINTER(module, GT_FreeDeviceList)
	  || !LOAD_FUNC_POINTER(module, GT_OpenDevice)
	  || !LOAD_FUNC_POINTER(module, GT_CloseDevice)
	  || !LOAD_FUNC_POINTER(module, GT_SetConfiguration)
	  || !LOAD_FUNC_POINTER(module, GT_GetConfiguration)
	  || !LOAD_FUNC_POINTER(module, GT_SetAsynchronConfiguration)
	  || !LOAD_FUNC_POINTER(module, GT_ApplyAsynchronConfiguration)
	  || !LOAD_FUNC_POINTER(module, GT_GetAsynchronConfiguration)
	  || !LOAD_FUNC_POINTER(module, GT_StartAcquisition)
	  || !LOAD_FUNC_POINTER(module, GT_StopAcquisition)
	  || !LOAD_FUNC_POINTER(module, GT_GetSamplesAvailable)
	  || !LOAD_FUNC_POINTER(module, GT_GetData)
	  || !LOAD_FUNC_POINTER(module, GT_SetDataReadyCallBack)
	  || !LOAD_FUNC_POINTER(module, GT_GetBandpassFilterListSize)
	  || !LOAD_FUNC_POINTER(module, GT_GetBandpassFilterList)
	  || !LOAD_FUNC_POINTER(module, GT_GetNotchFilterListSize)
	  || !LOAD_FUNC_POINTER(module, GT_GetNotchFilterList)
	  || !LOAD_FUNC_POINTER(module, GT_GetChannelCalibration)
	  || !LOAD_FUNC_POINTER(module, GT_SetChannelCalibration)
	  || !LOAD_FUNC_POINTER(module, GT_Calibrate)
	  || !LOAD_FUNC_POINTER(module, GT_GetImpedance) )
		return 0;

	return 1;
}


LOCAL_FN
int load_gusbamp_module(void)
{
	void* module = NULL;
	unsigned int i;
	int ret = -1;

	mm_thr_mutex_lock(&lock);

	if (gusbamp_numref > 0) {
		gusbamp_numref++;
		ret = 0;
		goto exit;
	}

	for (i = 0; ret && i < NUM_NAMES; i++) {
		module = mm_dlopen(gusbamp_name[i], MM_LD_LAZY);
		if (module && load_func_pointers(module))
			ret = 0;
	}

	if (ret) {
		fprintf(stderr, "failed to open gusbamp: %s\n", mm_strerror(errno));
		errno = ENOSYS;
		goto exit;
	}

	gusbamp_numref++;
	gusbamp_module = module;

exit:
	mm_thr_mutex_unlock(&lock);
	return ret;
}


LOCAL_FN
int unload_gusbamp_module(void)
{
	mm_thr_mutex_lock(&lock);
	if (--gusbamp_numref == 0)
		mm_dlclose(gusbamp_module);
	mm_thr_mutex_unlock(&lock);

	return 0;
}
