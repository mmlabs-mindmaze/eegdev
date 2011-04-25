/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <gAPI.h>

#include "eegdev-types.h"
#include "eegdev-common.h"
#include "devices.h"

struct gtec_eegdev {
	struct eegdev dev;
	int runacq;
	int buflen;
	void* buffer;
	char* devname;
	gt_size numdev;
	char** devlist;
	gt_usbamp_config config;
	gt_usbamp_asynchron_config asyncconf;
	gt_usbamp_analog_out_config ao_config;
	char prefiltering[128];
};

struct filtparam
{
	float order, fl, fh;
	int id;
};

#define get_gtec(dev_p) \
	((struct gtec_eegdev*)(((char*)(dev_p))-offsetof(struct gtec_eegdev, dev)))

#define SAMSIZE	(17*sizeof(float))

/*****************************************************************
 *                        gtec metadata                          *
 *****************************************************************/
static const char* eeglabel[] = {
	"eeg:1", "eeg:2", "eeg:3", "eeg:4", "eeg:5", "eeg:6",
	"eeg:7", "eeg:8", "eeg:9", "eeg:10", "eeg:11", "eeg:12",
	"eeg:13", "eeg:14", "eeg:15", "eeg:16"
};
static const char trigglabel[] = "Status";
static const char analog_unit[] = "uV";
static const char trigger_unit[] = "Boolean";
static const char analog_transducter[] = "Active Electrode";
static const char trigger_transducter[] = "Triggers and Status";
static const char trigger_prefiltering[] = "No filtering";
static const char gtec_device_type[] = "gTec g.USBamp";


/******************************************************************
 *                    open/close gTec device                      *
 ******************************************************************/
static
int gtec_open_first_device(struct gtec_eegdev* gtdev)
{
	unsigned int i;

	GT_UpdateDevices();
	gtdev->numdev = GT_GetDeviceListSize();
	if (gtdev->numdev == 0) {
		errno = ENODEV;
		return -1;
	}

	gtdev->devlist = GT_GetDeviceList();
	for (i=0; i<gtdev->numdev; i++)
		if (GT_OpenDevice(gtdev->devlist[i]) != GT_FALSE)
			break;

	if (i != gtdev->numdev)
		gtdev->devname = gtdev->devlist[i];
	else {
		GT_FreeDeviceList(gtdev->devlist, gtdev->numdev);
		gtdev->devlist = NULL;
		gtdev->numdev = 0;
		errno = EBUSY;
		return -1;
	}
	return 0;
}


static
void destroy_gtecdev(struct gtec_eegdev* gtdev)
{
	if (gtdev == NULL)
		return;

	if (gtdev->devname != NULL)
		GT_CloseDevice(gtdev->devname);

	if (gtdev->devlist != NULL) {
		GT_FreeDeviceList(gtdev->devlist, gtdev->numdev);
		gtdev->devlist = NULL;
		gtdev->numdev = 0;
	}

	egd_destroy_eegdev(&(gtdev->dev));
}


/******************************************************************
 *                       gTec configuration                       *
 ******************************************************************/
static
float valabs(float f) {return (f >= 0.0f) ? f : -f;} //avoid include libm


static 
int gtec_find_bpfilter(const struct gtec_eegdev *gtdev,
                       float fl, float fh, float order,
		       struct filtparam* filtprm)
{
	float score, minscore = 1e12;
	int i, best = -1, nfilt;
	gt_size fs = gtdev->config.sample_rate;
	gt_filter_specification *filt = NULL;

	// Get available filters
	nfilt = GT_GetBandpassFilterListSize(gtdev->devname, fs);
	filt = malloc(nfilt*sizeof(*filt));
	if (filt == NULL)
		return -1;
	GT_GetBandpassFilterList(gtdev->devname, fs, filt, 
	                           nfilt*sizeof(*filt));
	
	// Test matching score of each filter
	for (i=0; i<nfilt; i++) {
		score = valabs(fl-filt[i].f_lower)/fl
		       + valabs(fh-filt[i].f_upper)/fh
		       + valabs(order-filt[i].order)/order;
		if (score < minscore) {
			best = i;
			minscore = score;
		}
	}
	filtprm->id = filt[best].id;
	filtprm->order = filt[best].order;
	filtprm->fh = filt[best].f_upper;
	filtprm->fl = filt[best].f_lower;

	free(filt);
	return 0;
}


static 
int gtec_find_notchfilter(const struct gtec_eegdev *gtdev, float freq,
                          struct filtparam* filtprm)
{
	float score, minscore = 1e12;
	int i, best = -1, nfilt;
	gt_size fs = gtdev->config.sample_rate;
	gt_filter_specification *filt = NULL;

	// Get available filters
	nfilt = GT_GetNotchFilterListSize(gtdev->devname, fs);
	filt = malloc(nfilt*sizeof(*filt));
	if (filt == NULL)
		return -1;
	GT_GetNotchFilterList(gtdev->devname, fs, filt, 
	                           nfilt*sizeof(*filt));
	
	// Test matching score of each filter
	for (i=0; i<nfilt; i++) {
		score = valabs(freq-0.5*(filt[i].f_lower+filt[i].f_upper));
		if (score < minscore) {
			best = i;
			minscore = score;
		}
	}

	filtprm->id = filt[best].id;
	filtprm->order = filt[best].order;
	filtprm->fh = filt[best].f_upper;
	filtprm->fl = filt[best].f_lower;

	free(filt);
	return 0;
}


static
void gtec_setup_eegdev_core(struct gtec_eegdev* gtdev)
{
	// Advertise capabilities
	gtdev->dev.cap.type_nch[EGD_EEG] = 16;
	gtdev->dev.cap.type_nch[EGD_SENSOR] = 0;
	gtdev->dev.cap.type_nch[EGD_TRIGGER] = 1;
	gtdev->dev.cap.sampling_freq = gtdev->config.sample_rate;
	gtdev->dev.cap.device_type = gtec_device_type;
	gtdev->dev.cap.device_id = gtdev->devname;

	// inform the ringbuffer about the size of one sample
	egd_set_input_samlen(&(gtdev->dev), SAMSIZE);

	egd_update_capabilities(&(gtdev->dev));
}


// Common configurations
static gt_usbamp_analog_out_config ao_config = {
	.shape = GT_ANALOGOUT_SINE,
	.frequency = 10,
	.amplitude = 5,
	.offset = 0
};
static gt_usbamp_asynchron_config asynchron_config = {
	.digital_out = {GT_FALSE, GT_FALSE, GT_FALSE, GT_FALSE}
};


static
int gtec_configure_device(struct gtec_eegdev *gtdev)
{
	int i;
	gt_usbamp_config* conf = &(gtdev->config);
	struct filtparam bpprm, notchprm;
	int fs = 512;

	conf->ao_config = &ao_config;
	conf->sample_rate = fs;
	conf->number_of_scans = GT_NOS_AUTOSET;
	conf->enable_trigger_line = GT_TRUE;
	conf->scan_dio = GT_TRUE;
	conf->slave_mode = GT_FALSE;
	conf->enable_sc = GT_FALSE;
	conf->mode = GT_MODE_NORMAL;
	conf->num_analog_in = 16;
	
	// Set all common reference and ground
	for (i=0; i<GT_USBAMP_NUM_REFERENCE; i++) {
		conf->common_ground[i] = GT_TRUE;
		conf->common_reference[i] = GT_TRUE;
	}

	// find best filters
	if (gtec_find_bpfilter(gtdev, 0.1, 0.4*fs, 2, &bpprm)
	    || gtec_find_notchfilter(gtdev, 50, &notchprm))
		return -1;
	
	// Setup prefiltering string
	snprintf(gtdev->prefiltering, sizeof(gtdev->prefiltering)-1,
	        "LP: %.1f Hz; HP: %.1f Hz; Notch: %.1f Hz",
	        bpprm.fl, bpprm.fh, 0.5*(notchprm.fl+notchprm.fh));

	// Set channel params
	for (i=0; i<GT_USBAMP_NUM_ANALOG_IN; i++) {
		conf->bandpass[i] = bpprm.id;
		conf->notch[i] = notchprm.id;
		conf->bipolar[i] = GT_BIPOLAR_DERIVATION_NONE;
		conf->analog_in_channel[i] = i+1;
	}

	gtec_setup_eegdev_core(gtdev);
	GT_SetConfiguration(gtdev->devname, conf);
	GT_SetAsynchronConfiguration(gtdev->devname, &asynchron_config);
	GT_ApplyAsynchronConfiguration(gtdev->devname);

	return 0;
}


/******************************************************************
 *                       gTec acquisition                         *
 ******************************************************************/
static
void gtec_callback(void* data)
{
	struct gtec_eegdev* restrict gtdev = data;
	void* restrict buffer = gtdev->buffer;
	int sizetot, size, buflen = gtdev->buflen;
	
	// Transfer data to ringbuffer by chunks of buflen bytes max
	sizetot = GT_GetSamplesAvailable(gtdev->devname);
	while (sizetot > 0) {
		size = (sizetot < buflen) ? sizetot : buflen;
		size = GT_GetData(gtdev->devname, buffer, size);
		if (size <= 0) {
			egd_report_error(&(gtdev->dev), ENOMEM);
			return;
		}

		egd_update_ringbuffer(&(gtdev->dev), buffer, size);
		sizetot -= size;
	}
}


static
int gtec_start_device_acq(struct gtec_eegdev* gtdev)
{
	// prepare small buffer
	gtdev->buflen = 0.1 * (double)gtdev->config.sample_rate;
	gtdev->buflen *= SAMSIZE;
	gtdev->buffer = malloc(gtdev->buflen);

	// Start device acquisition
	GT_SetDataReadyCallBack(gtdev->devname, gtec_callback, gtdev);
	GT_StartAcquisition(gtdev->devname);
	return 0;
}

static
int gtec_stop_device_acq(struct gtec_eegdev* gtdev)
{
	// Start device acquisition
	GT_StopAcquisition(gtdev->devname);

	// prepare small buffer
	free(gtdev->buffer);

	return 0;
}


/******************************************************************
 *                  gTec methods implementation                   *
 ******************************************************************/
static 
int gtec_close_device(struct eegdev* dev)
{
	struct gtec_eegdev* gtdev = get_gtec(dev);
	
	gtec_stop_device_acq(gtdev);
	destroy_gtecdev(gtdev);
	free(gtdev);

	return 0;
}


static 
int gtec_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	unsigned int i;
	struct selected_channels* selch = dev->selch;
	unsigned int offsets[EGD_NUM_STYPE] = {
		[EGD_EEG] = 0,
		[EGD_TRIGGER] = 16*sizeof(float),
		[EGD_SENSOR] = SAMSIZE,
	};
	
	for (i=0; i<ngrp; i++) {
		// Set parameters of (eeg -> ringbuffer)
		selch[i].in_offset = offsets[grp[i].sensortype]
		                     + grp[i].index*sizeof(float);
		selch[i].inlen = grp[i].nch*sizeof(float);
		selch[i].typein = EGD_FLOAT;
		selch[i].bsc = 0;
	}
		
	return 0;
}


static 
void gtec_fill_chinfo(const struct eegdev* dev, int stype,
	                     unsigned int ich, struct egd_chinfo* info)
{
	if (stype != EGD_TRIGGER) {
		info->isint = 0;
		info->dtype = EGD_DOUBLE;
		info->min.dval = -262144.0;
		info->max.dval = 262143.96875;
		info->label = eeglabel[ich];
		info->unit = analog_unit;
		info->transducter = analog_transducter;
		info->prefiltering = get_gtec(dev)->prefiltering;
	} else {
		info->isint = 1;
		info->dtype = EGD_INT32;
		info->min.i32val = -8388608;
		info->max.i32val = 8388607;
		info->label = trigglabel;
		info->unit = trigger_unit;
		info->transducter = trigger_transducter;
		info->prefiltering = trigger_prefiltering;
	}
}


static
int gtec_noaction(struct eegdev* dev)
{
	(void)dev;
	return 0;
}


LOCAL_FN
struct eegdev* open_gtec(const struct opendev_options* opt)
{
	struct eegdev_operations gtec_ops = {
		.close_device = gtec_close_device,
		.start_acq = gtec_noaction,
		.stop_acq = gtec_noaction,
		.set_channel_groups = gtec_set_channel_groups,
		.fill_chinfo = gtec_fill_chinfo
	};
	struct gtec_eegdev* gtdev = NULL;
	(void)opt;

	// alloc and initialize structure and open the device
	if ((gtdev = calloc(1, sizeof(*gtdev))) == NULL
	 || egd_init_eegdev(&(gtdev->dev), &gtec_ops)
	 || gtec_open_first_device(gtdev)
	 || gtec_configure_device(gtdev)
	 || gtec_start_device_acq(gtdev)) {
		// failure: clean up
		destroy_gtecdev(gtdev);
		free(gtdev);
		return NULL;
	}

	return &(gtdev->dev);
}

