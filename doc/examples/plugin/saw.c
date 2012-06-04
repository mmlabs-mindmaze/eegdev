/*
    Copyright (C) 2011-2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

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
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>

#include <eegdev-pluginapi.h>


struct saw_eegdev {
	struct devmodule dev;
	int fs;
	pthread_t thread_id;
	
	char tmplabel[16];
};

#define get_saw(dev_p) ((struct saw_eegdev*)(dev_p))

#define SAMPLINGRATE	256
#define NUM_EEG_CH	8
#define NUM_TRI_CH	1
#define NS		2	// Number of sample transfered in one update
#define SAWFREQ		50	// Frequency of the sawtooth function in
				// number of sample


#define NCH	(NUM_EEG_CH + NUM_TRI_CH)

#define NSEC_IN_SEC	1000000000

/******************************************************************
 *                       sawtooth metadata                   	  *
 ******************************************************************/
static const char saw_device_type[] = "Sawtooth function generator";
static const char saw_device_id[] = "N/A";
static const struct egdi_signal_info saw_siginfo[2] = {
	{
		.isint = 0, .bsc = 1, .scale= 1.0/8192.0,
		.dtype = EGD_FLOAT, .mmtype = EGD_DOUBLE,
		.min.valdouble = -262144.0, .max.valdouble = 262143.96875,
		.unit = "uV", .transducer = "Fake electrode"
	}, {
		.isint = 1, .bsc = 0,
		.dtype = EGD_FLOAT, .mmtype = EGD_DOUBLE,
		.min.valint32_t = INT32_MIN, .max.valint32_t = INT32_MAX,
		.unit = "Boolean", .transducer = "Trigger"
	}
};
static const struct egdi_optname saw_options[] = {
	{.name = "samplingrate", .defvalue = "256"},
	{.name = NULL}
};

static
void sawtooth_func(int32_t* data, long isample)
{
	int i;

	for (i=0; i<NUM_EEG_CH; i++)
		data[i] = (i+1)*((isample % SAWFREQ) - SAWFREQ/2);

	for (i=0; i<NUM_TRI_CH; i++)
		data[i+NUM_EEG_CH] = (isample % SAWFREQ) ? 0 : (0xAA << i);
}


/* Acquisition loop function

Comment: This function use clock_gettime and clock_nanosleep. Although those
functions are part of POSIX.1-2001, they are not always present by default
on some platform (MacOSX and Windows). If this code is only meant for
testing purpose, just replace them with equivalents. If this code is meant
for real device implementation, keep using them but provides replacement
for platform that don't have them (see the source code of eegdev for an
example of how to do it).
*/
static
void* acq_loop_fn(void* arg)
{
	struct saw_eegdev* sawdev = arg;
	const struct core_interface* ci = &sawdev->dev.ci;
	struct devmodule* dev = &sawdev->dev;
	int32_t data[NCH*NS] = {0};
	struct timespec ts;
	long isample = 0;
	int i;

	// Initialize ts with the current time
	// (note: see earlier comment)
	clock_gettime(CLOCK_REALTIME, &ts);

	// Make sure that we can call pthread_cancel from the main thread
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	while (1) {
		// Test whether the acquisition thread should stop.
		// This is often not necessary to call it with non trivial
		// device since those often call for their acquisition
		// function that are cancellation point.
		// (see pthreads manpage)
		pthread_testcancel();

		// Set the timestamp to the next transfer and wait for it.
		ts.tv_nsec += NS*(NSEC_IN_SEC / sawdev->fs);
		if (ts.tv_nsec >= NSEC_IN_SEC) {
			ts.tv_nsec -= NSEC_IN_SEC;
			ts.tv_sec++;
		}
		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

		// Write sample in the array (implement sawtooth function)
		for (i=0; i<NS; i++)
			sawtooth_func(&data[i*NCH], isample++);

		// Update the eegdev structure with the new data
		// Failure in this function already report error in the core
		if (ci->update_ringbuffer(dev, data, sizeof(data)))
			break;
	}
	
	// If we reach here, an error has occured before
	return NULL;
}


/******************************************************************
 *               sawtooth methods implementation                  *
 ******************************************************************/
static
int saw_open_device(struct devmodule* dev, const char* optv[])
{
	int ret, i, j, stype, nch[2] = {NUM_EEG_CH, NUM_TRI_CH};
	pthread_t* pthid;
	struct egdi_chinfo chmap[NUM_EEG_CH + NUM_TRI_CH] = {{.label=NULL}};
	struct saw_eegdev* sawdev = get_saw(dev);
	struct systemcap cap;
	const char* typename[2] = {"eeg", "trigger"};

	// The core library populates optv array with the setting values in
	// the order of their declaration in the array assigned in the
	// supported_opts field of the egdi_plugin_info structure. If the
	// user does not specify any value for a specific setting, it
	// receive its default value as defined in the defvalue field.
	// So in this particular case, optv[0] correspond to the value of
	// "samplerate" setting whose default value is "256" (see the
	// previous declaration of saw_options).
	sawdev->fs = atoi(optv[0]);

	// Setup the channel map
	cap.nch = 0;
	for (j=0; j<2; j++) {
		stype = egd_sensor_type(typename[j]);
		for (i=0; i<nch[j]; i++) {
			chmap[i+cap.nch].stype = stype;
			chmap[i+cap.nch].si = &saw_siginfo[j];
		}
		cap.nch += nch[j];
	}

	// Specify the capabilities of a saw device
	cap.sampling_freq = sawdev->fs;
	cap.device_type = saw_device_type;
	cap.device_id = saw_device_id;
	cap.chmap = chmap;
	dev->ci.set_cap(dev, &cap);
	dev->ci.set_input_samlen(dev, NCH*sizeof(int32_t));

	// Create the acquisition thread
	pthid = &sawdev->thread_id;
	ret = pthread_create(pthid, NULL, acq_loop_fn, sawdev);
	if (ret) {
		errno = ret;
		return -1;
	}
	
	return 0;
}


static
int saw_close_device(struct devmodule* dev)
{
	struct saw_eegdev* sawdev = get_saw(dev);

	// Stop acquisition thread
	pthread_cancel(sawdev->thread_id);
	pthread_join(sawdev->thread_id, NULL);
	
	return 0;
}


static
int saw_set_channel_groups(struct devmodule* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	unsigned int i, t;
	struct selected_channels* selch;
	const int soff[2] = {0, NUM_EEG_CH};
	int trigg_stype = egd_sensor_type("trigger");
	
	if (!(selch = dev->ci.alloc_input_groups(dev, ngrp)))
		return -1;

	for (i=0; i<ngrp; i++) {
		t = (grp[i].sensortype == trigg_stype) ? 1 : 0;

		// Set parameters of (eeg -> ringbuffer)
		selch[i].in_offset = (soff[t]+grp[i].index)*sizeof(int32_t);
		selch[i].inlen = grp[i].nch*sizeof(int32_t);
		selch[i].typein = EGD_INT32;
		selch[i].typeout = grp[i].datatype;
		selch[i].iarray = grp[i].iarray;
		selch[i].arr_offset = grp[i].arr_offset;
		if (t == 1)
			selch[i].bsc = 0;
		else {
			selch[i].bsc = 1;
			selch[i].sc.valfloat = (1.0f/8192.0f);
		}
	}
		
	return 0;
}


static
void saw_fill_chinfo(const struct devmodule* dev, int stype,
	             unsigned int ich, struct egdi_chinfo* info,
		     struct egdi_signal_info* si)
{
	int t;
	struct saw_eegdev* sawdev = get_saw(dev); 

	t = (stype == EGD_EEG) ? 0 : 1;
	memcpy(si, &saw_siginfo[t], sizeof(*si));
	sprintf(sawdev->tmplabel, (t ? "tri:%i":"eeg:%i"), ich);
	info->label = sawdev->tmplabel;
}


// All the device methods of the plugin are declared here.
// They _must_ be defined in a struct egdi_plugin_info named
// eegdev_plugin_info which is usually the only symbol exported by the
// dynamically shared object.
API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct saw_eegdev),
	.open_device = 		saw_open_device,
	.close_device = 	saw_close_device,
	.set_channel_groups = 	saw_set_channel_groups,
	.fill_chinfo = 		saw_fill_chinfo,
	.supported_opts =	saw_options
};

