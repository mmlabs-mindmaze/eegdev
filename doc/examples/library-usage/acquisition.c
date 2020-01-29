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
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <mmthread.h>
#include <eegdev.h>
#include <xdfio.h>

#include "acquisition.h"
#define CHUNK_NS	4

static const enum xdftype egd_to_xdf[] = {
	[EGD_INT32] = XDFINT32,
	[EGD_FLOAT] = XDFFLOAT,
	[EGD_DOUBLE] = XDFDOUBLE
};

struct acq {
	struct grpconf grp[3];
	size_t strides[3];
	void* buff[3];
	struct xdf* xdf;
	struct eegdev* dev;
	
	mm_thread_t thid;
	mm_thr_mutex_t lock;
	mm_thr_cond_t cond;
	int running;

	acqcb cb;
	void* cbdata;
};


static
void* acq_loop_fn(void* arg) {
	struct acq* acq = arg;
	struct eegdev* dev = acq->dev;
	void** buff = acq->buff;
	int lrun;
	ssize_t ns;

	
	while (1) {
		// Update status of the loop
		mm_thr_mutex_lock(&acq->lock);
		while (!(lrun = acq->running)) {
			mm_thr_cond_wait(&acq->cond, &acq->lock);
		}
		mm_thr_mutex_unlock(&acq->lock);
		if (lrun < 0)
			break;

		// Perform the actual transfer from dev to buffers
		ns = egd_get_data(dev, CHUNK_NS, buff[0], buff[1], buff[2]);
		if (ns <= 0) 
			break;
		// Writes data to file
		if (xdf_write(acq->xdf, ns, buff[0], buff[1], buff[2]) < 0)
			break;
		
		// Execute callback
		if (acq->cb)
			acq->cb(acq->cbdata, ns, buff[0], buff[1], buff[2]);
	}

	return NULL;
}


static
int configure_egd_acq(struct acq* acq)
{
	int i, stype, isint;
	const char* const types[3] = {"eeg", "undefined", "trigger"};
	struct eegdev* dev = acq->dev;
	
	for (i=0; i<3; i++) {
		stype = egd_sensor_type(types[i]);
		egd_channel_info(dev, stype, 0, EGD_ISINT, &isint, EGD_EOL);
		acq->grp[i].sensortype = stype;
		acq->grp[i].index = 0;
		acq->grp[i].nch = egd_get_numch(dev, stype);
		acq->grp[i].iarray = i;
		acq->grp[i].arr_offset = 0;
		acq->grp[i].datatype = (isint) ? EGD_INT32 : EGD_FLOAT;
		acq->strides[i] = (stype == EGD_TRIGGER) ?
		                          sizeof(int32_t) : sizeof(float);
		acq->strides[i] *= acq->grp[i].nch;

		if (!(acq->buff[i] = malloc(acq->strides[i]*CHUNK_NS)))
			break;
	}

	return (i == 3) ? 0 : -1;;
}


struct acq* acq_init(const char* devstring, acqcb cb, void* cbdata)
{
	struct acq* acq;
	int i;

	if (!(acq = calloc(sizeof(*acq), 1)))
		return NULL;

	// Open the connection to the device
	if (!(acq->dev = egd_open(devstring))) {
		fprintf(stderr, "Connection error: %s\n",strerror(errno));
		goto exit;
	}

	// Setup the transfer to the data buffers
	if (configure_egd_acq(acq)
	    || egd_acq_setup(acq->dev, 3, acq->strides, 3, acq->grp)) {
		fprintf(stderr, "Acq_setup: %s\n", strerror(errno));
		goto exit;
	}

	acq->cb = cb;
	acq->cbdata = cbdata ? cbdata : acq;
	if (mm_thr_create(&acq->thid, acq_loop_fn, acq))
		goto exit;

	return acq;

exit:
	if (acq->dev) 
		egd_close(acq->dev);
	for (i=0; i<3; i++)
		free(acq->buff[i]);
	free(acq);
	return NULL;
}


void acq_close(struct acq* acq)
{
	if (!acq)
		return;

	// Inform the acquisition thread to stop
	mm_thr_mutex_lock(&acq->lock);
	acq->running = -1;
	mm_thr_cond_signal(&acq->cond);
	mm_thr_mutex_unlock(&acq->lock);

	// Wait for the thread to actually finish
	mm_thr_join(acq->thid, NULL);
	
	// Close device
	egd_close(acq->dev);
	free(acq);
}


int acq_get_info(struct acq* acq, int type)
{
	int retval;

	if (!acq)
		return -EINVAL;

	if (type == ACQ_FS)
		retval = egd_get_cap(acq->dev, EGD_CAP_FS, NULL);
	else if (type == ACQ_NEEG)
		retval = acq->grp[0].nch;
	else if (type == ACQ_NSENS)
		retval = acq->grp[1].nch;
	else if (type == ACQ_NTRI)
		retval = acq->grp[2].nch;
	else
		retval = -EINVAL;
	
	return retval;
}


static
int setup_xdf_channel_group(struct acq* acq, int igrp, struct xdf* xdf)
{
	char label[32], transducer[128], unit[16], filtering[128];
	double mm[2];
	unsigned int j;
	struct grpconf* grp = acq->grp + igrp;

	egd_channel_info(acq->dev, grp->sensortype, grp->index,
			 EGD_UNIT, unit,
			 EGD_TRANSDUCER, transducer,
			 EGD_PREFILTERING, filtering,
			 EGD_MM_D, mm,
			 EGD_EOL);

	xdf_set_conf(xdf, XDF_CF_ARRINDEX, grp->iarray,
		          XDF_CF_ARROFFSET, grp->arr_offset,
		          XDF_CF_ARRDIGITAL, 0,
		          XDF_CF_ARRTYPE, egd_to_xdf[grp->datatype],
		          XDF_CF_PMIN, mm[0],
		          XDF_CF_PMAX, mm[1],
		          XDF_CF_TRANSDUCTER, transducer,
	                  XDF_CF_PREFILTERING, filtering,
		          XDF_CF_UNIT, unit,
		          XDF_NOF);

	for (j = 0; j < grp->nch; j++) {
		egd_channel_info(acq->dev, grp->sensortype, j,
		                 EGD_LABEL, label, EGD_EOL);

		// Add the channel to the BDF
		if (xdf_add_channel(xdf, label) == NULL)
			return -1;
	}
	return 0;
}


int acq_prepare_rec(struct acq* acq, const char* filename)
{
	struct xdf* xdf;
	int j, fs;

	if (!acq || !filename)
		return 0;

	// Create the BDF file
	xdf = xdf_open(filename, XDF_WRITE, XDF_BDF);
	if (!xdf) 
		goto abort;

	// Configuration file general header
	fs = egd_get_cap(acq->dev, EGD_CAP_FS, NULL);
	xdf_set_conf(xdf, XDF_F_REC_DURATION, 1.0,
	                  XDF_F_REC_NSAMPLE, fs,
		          XDF_NOF);

	// Set up the file recording channels
	for (j=0; j<3; j++)	
		if (setup_xdf_channel_group(acq, j, xdf))
			goto abort;

	// Make the file ready for recording
	xdf_define_arrays(xdf, 3, acq->strides);
	if (xdf_prepare_transfer(xdf))
		goto abort;

	acq->xdf = xdf;
	return 0;
	
abort:
	fprintf(stderr, "Preparing recording file: %s\n", strerror(errno));
	xdf_close(xdf);
	return -1;
}


int acq_start(struct acq* acq)
{
	if (!acq || !acq->xdf)
		return -1;

	egd_start(acq->dev);

	// Inform the acquisition thread to run
	mm_thr_mutex_lock(&acq->lock);
	acq->running = 1;
	mm_thr_cond_signal(&acq->cond);
	mm_thr_mutex_unlock(&acq->lock);

	return 0;
}


int acq_stop(struct acq* acq)
{
	if (!acq || !acq->xdf)
		return -1;

	// Inform the acquisition thread to run
	mm_thr_mutex_lock(&acq->lock);
	acq->running = 0;
	mm_thr_cond_signal(&acq->cond);
	mm_thr_mutex_unlock(&acq->lock);

	xdf_close(acq->xdf);
	acq->xdf = NULL;

	return 0;
}

