/*
    Copyright (C) 2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
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

#include <mmlib.h>
#include <mmsysio.h>
#include <mmthread.h>
#include <mmtime.h>
#include <string.h>
#if DLOPEN_GUSBAMP
# include "src/plugins/gusbamp-types.h"
#else
# include <gAPI.h>
#endif

#include "fakegtec.h"

struct gtec_device {
	const char* devname;
	int inuse, running;
	mmthr_mtx_t lock;
	gt_usbamp_config conf;
	gt_usbamp_asynchron_config as_conf;
	unsigned int nsample, lastsample;
	mmthread_t thid;
	mmthr_mtx_t updatelock;
	mmthr_cond_t cond;
	void (*callback)(void*);
	void *callback_data;
};

static const char* devname[] = {
	"UB-2009.10.06",
	"UB-2009.10.10",
	"UB-2009.10.21",
	"UB-2009.10.22",
	"UB-2009.11.22"
};
#define NUMDEV	((int)(sizeof(devname)/sizeof(devname[0])))

static struct timespec org;
static int acquiring = 0;
static mmthr_mtx_t acqlock = MMTHR_MTX_INITIALIZER;
static mmthr_cond_t acqcond = MMTHR_COND_INITIALIZER;

static struct gtec_device  gtdevices[NUMDEV];

static
struct gtec_device* get_dev(const char* dname, int *id)
{
	int i;
	struct gtec_device* dev = NULL;

	for (i=0; i<NUMDEV; i++)
		if (!strcmp(devname[i], dname)) {
			dev = &(gtdevices[i]);
			break;
		}

	if (dev && id)
		*id = i;
		
	return dev;
}


static
void initialize_device(struct gtec_device* gtdev)
{
	gtdev->inuse = 0;
	mmthr_mtx_init(&(gtdev->lock), 0);
	mmthr_mtx_init(&(gtdev->updatelock), 0);
}


static
void* update_thread(void* data)
{
	struct timespec ts;
	int ret;
	unsigned int diff, ntot, seed;
	struct gtec_device* gtdev = data;
	mmthr_mtx_t* lock = &(gtdev->updatelock);
	struct random_data rdata = {.rand_type = 0};
	int32_t randnum = 0;
	char state[128] = {0};

	// Initialize random generator
	mm_gettime(MM_CLK_REALTIME, &ts);
	seed = ts.tv_nsec;
	initstate_r(seed, state, sizeof(state), &rdata);

	// Wait for acquisition start
	mmthr_mtx_lock(&acqlock);
	while (!acquiring) {
		mmthr_cond_wait(&acqcond, &acqlock);	
	}
	mmthr_mtx_unlock(&acqlock);
	
	memcpy(&ts, &org, sizeof(ts));
	random_r(&rdata, &randnum);
	mm_timeadd_ns(&ts, 7000000 + randnum/2500);

	mmthr_mtx_lock(lock);
	while (gtdev->running) {
		ret = mmthr_cond_timedwait(&gtdev->cond, lock, &ts);
		if (ret == ETIMEDOUT) {
			diff = mm_timediff_ms(&ts, &org);
			ntot = (diff*gtdev->conf.sample_rate)/1000;
			gtdev->nsample = ntot;

			mmthr_mtx_unlock(lock);
			gtdev->callback(gtdev->callback_data);
			random_r(&rdata, &randnum);
			mm_timeadd_ns(&ts, 7000000 + randnum/2500);
			mmthr_mtx_lock(lock);
		}
	}
	mmthr_mtx_unlock(lock);

	return NULL;	
}


API_EXPORTED
void GT_ShowDebugInformation( gt_bool show )
{
	(void)show;	
}


API_EXPORTED
gt_bool   GT_UpdateDevices()
{
	static int isinit = 0;
	int i;

	if (isinit)
		return GT_TRUE;

	for (i=0; i<NUMDEV; i++)
		initialize_device(&gtdevices[i]);

	isinit = 1;

	return GT_TRUE;
}


API_EXPORTED
gt_size GT_GetDeviceListSize()
{
	return NUMDEV;
}


API_EXPORTED
char** GT_GetDeviceList()
{
	int i;
	char** devlist;
	
	devlist = malloc(NUMDEV*sizeof(devlist[0]));
	for (i=0; i<NUMDEV; i++) {
		devlist[i] = malloc(strlen(devname[i])+1);
		strcpy(devlist[i], devname[i]);
	}
	
	return devlist;
}


API_EXPORTED
gt_bool   GT_FreeDeviceList( char** device_list, gt_size list_size )
{
	gt_size i;

	for (i=0; i<list_size; i++)
		free(device_list[i]);
	free(device_list);
	return GT_TRUE;
}



API_EXPORTED
gt_bool GT_OpenDevice( const char* device_name )
{
	gt_bool retval = GT_TRUE;
	struct gtec_device* gtdev;
	
	gtdev = get_dev(device_name, NULL);
	if (!gtdev)
		return GT_FALSE;
	mmthr_mtx_lock(&gtdev->lock);
	if (gtdev->inuse)
		retval = 0;
	else
		gtdev->inuse = 1;
	mmthr_mtx_unlock(&gtdev->lock);

	return retval;
}


API_EXPORTED
gt_bool GT_CloseDevice( const char* device_name )
{
	gt_bool retval = GT_TRUE;
	struct gtec_device* gtdev;
	
	gtdev = get_dev(device_name, NULL);
	if (!gtdev)
		return GT_FALSE;

	mmthr_mtx_lock(&gtdev->lock);
	if (!gtdev->inuse)
		retval = 0;
	else
		gtdev->inuse = 0;
	mmthr_mtx_unlock(&gtdev->lock);

	return retval;
}



API_EXPORTED
gt_bool GT_SetConfiguration( const char* device_name, void* configuration )
{
	struct gtec_device* gtdev;
	
	gtdev = get_dev(device_name, NULL);
	if (!gtdev)
		return GT_FALSE;
	
	memcpy(&(gtdev->conf), configuration, sizeof(gtdev->conf));
	return GT_TRUE;
}


API_EXPORTED
gt_bool GT_GetConfiguration( const char* device_name, void* configuration )
{
	struct gtec_device* gtdev;
	
	gtdev = get_dev(device_name, NULL);
	if (!gtdev)
		return GT_FALSE;
	
	memcpy(configuration, &(gtdev->conf), sizeof(gtdev->conf));
	return GT_TRUE;
}


API_EXPORTED
gt_bool GT_SetAsynchronConfiguration( const char* device_name, void* configuration )
{
	struct gtec_device* gtdev;
	
	gtdev = get_dev(device_name, NULL);
	if (!gtdev)
		return GT_FALSE;
	
	memcpy(&(gtdev->as_conf), configuration, sizeof(gtdev->as_conf));
	return GT_TRUE;
}


API_EXPORTED
gt_bool GT_ApplyAsynchronConfiguration( const char* device_name )
{
	return get_dev(device_name, NULL) ? GT_TRUE : GT_FALSE;
}


API_EXPORTED
gt_bool GT_GetAsynchronConfiguration( const char* device_name, void* configuration )
{
	struct gtec_device* gtdev;
	
	gtdev = get_dev(device_name, NULL);
	if (!gtdev)
		return GT_FALSE;
	
	memcpy(configuration, &(gtdev->as_conf), sizeof(gtdev->as_conf));
	return GT_TRUE;
}


API_EXPORTED
gt_bool GT_StartAcquisition( const char* device_name )
{
	struct gtec_device* gtdev;
	
	gtdev = get_dev(device_name, NULL);
	if (!gtdev)
		return GT_FALSE;
	
	if (gtdev->running)
		return GT_FALSE;

	gtdev->running = 1;
	mmthr_create(&(gtdev->thid), update_thread, gtdev);
	
	if (!gtdev->conf.slave_mode) {
		mm_gettime(MM_CLK_REALTIME, &org);
		mmthr_mtx_lock(&acqlock);
		acquiring = 1;
		mmthr_cond_broadcast(&acqcond);
		mmthr_mtx_unlock(&acqlock);
	}

	return GT_TRUE;
}


API_EXPORTED
gt_bool GT_StopAcquisition( const char* device_name )
{
	struct gtec_device* gtdev;
	int retval = GT_FALSE;
	
	gtdev = get_dev(device_name, NULL);
	if (!gtdev)
		return GT_FALSE;
	
	mmthr_mtx_lock(&gtdev->updatelock);
	if (!gtdev->running)
		retval = GT_FALSE;
	else {
		gtdev->running = 0;
		mmthr_cond_signal(&gtdev->cond);
	}
	mmthr_mtx_unlock(&gtdev->updatelock);
	mmthr_join(gtdev->thid, NULL);

	if (!gtdev->conf.slave_mode) {
		mmthr_mtx_lock(&acqlock);
		acquiring = 0;
		mmthr_mtx_unlock(&acqlock);
	}

	return retval;
}


API_EXPORTED
int  GT_GetSamplesAvailable( const char* device_name )
{
	struct gtec_device* gtdev;
	unsigned int ntot;
	
	gtdev = get_dev(device_name, NULL);
	if (!gtdev)
		return -1;

	mmthr_mtx_lock(&gtdev->updatelock);
	ntot = gtdev->nsample;
	mmthr_mtx_unlock(&gtdev->updatelock);
	return (ntot - gtdev->lastsample)*17*sizeof(float);
}


API_EXPORTED
int  GT_GetData( const char* device_name, unsigned char* buffer, gt_size num_samples )
{
	unsigned int last, j, s, ns;
	struct gtec_device* gtdev;
	float* data = (float*)buffer;
	int idev = 0;
	
	gtdev = get_dev(device_name, &idev);
	if (!gtdev)
		return -1;
	
	ns = num_samples/(sizeof(*data)*17);
	last = gtdev->lastsample;
	
	/*if (ns+last > 1024)
		ns = 1024 - last;
	if (ns <= 0)
		exit(EXIT_FAILURE);
	num_samples = ns * (sizeof(*data)*17);*/

	for (s=0; s<ns; s++) {
		for (j=0; j<16; j++)
			data[s*17 + j] = get_analog_val(s+last, idev*16+j);
		data[s*17+16] = get_trigger_val(s+last, idev);
	}

	gtdev->lastsample += ns;
	return num_samples;
}


API_EXPORTED
gt_bool GT_SetDataReadyCallBack( const char* device_name, void (*callback_function)(void*), void* data)
{
	struct gtec_device* gtdev;
	
	gtdev = get_dev(device_name, NULL);
	if (!gtdev)
		return GT_FALSE;
	
	gtdev->callback = callback_function;
	gtdev->callback_data = data;
	return GT_TRUE;
}


/*------------------------------------------------------------------------------
 * g.tec g.USBamp specific API functions
 */


API_EXPORTED
gt_size GT_GetBandpassFilterListSize( const char* device_name, gt_size sample_rate )
{
	(void)device_name;
	(void)sample_rate;
	return 1;
}


API_EXPORTED
gt_bool GT_GetBandpassFilterList( const char* device_name, gt_size sample_rate, gt_filter_specification* filter, gt_size filter_size )
{
	(void)device_name;
	(void)sample_rate;
	(void)filter_size;
	gt_filter_specification filt = {
  		.f_upper = 200.0, .f_lower = 0.1, .sample_rate = 512,
		.order = 4, .id = 1
	};
	memcpy(filter, &filt, sizeof(filt));
	return GT_TRUE;
}


API_EXPORTED
gt_size GT_GetNotchFilterListSize( const char* device_name, gt_size sample_rate )
{
	(void)device_name;
	(void)sample_rate;
	return 1;
}


API_EXPORTED
gt_bool GT_GetNotchFilterList( const char* device_name, gt_size sample_rate, gt_filter_specification* filter, gt_size filter_size )
{
	(void)device_name;
	(void)sample_rate;
	(void)filter_size;
	gt_filter_specification filt = {
  		.f_upper = 51, .f_lower = 49, .sample_rate = 512,
		.order = 4, .id = 1
	};
	memcpy(filter, &filt, sizeof(filt));
	return GT_TRUE;
}


API_EXPORTED
gt_bool GT_GetChannelCalibration( const char* device_name, gt_usbamp_channel_calibration* calibration )
{
	(void)device_name;
	(void)calibration;
	return GT_TRUE;
}


API_EXPORTED
gt_bool GT_SetChannelCalibration( const char* device_name, gt_usbamp_channel_calibration* calibration )
{
	(void)device_name;
	(void)calibration;
	return GT_TRUE;
}


API_EXPORTED
gt_bool GT_Calibrate( const char* device_name, gt_usbamp_channel_calibration* calibration )
{
	(void)device_name;
	(void)calibration;
	return GT_TRUE;
}


API_EXPORTED
gt_bool GT_GetImpedance( const char* device_name, gt_size channel, int* impedance )
{
	(void)device_name;
	(void)channel;
	(void)impedance;
	return GT_TRUE;
}

