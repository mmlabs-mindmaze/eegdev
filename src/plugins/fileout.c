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
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <xdfio.h>
#include <errno.h>
#include <time.h>
#include <regex.h>

// Replacement declarations: it uses the proper declaration if the function
// is declared on the system
#include <portable-time.h>

#include <eegdev-common.h>

struct xdfout_eegdev {
	struct eegdev dev;
	pthread_t thread_id;
	pthread_cond_t runcond;
	pthread_mutex_t runmtx;
	int runstate;
	int *stypes;
	void* chunkbuff;
	size_t chunksize;
	struct xdf* xdf;
	struct timespec start_ts;
};

#define get_xdf(dev_p) ((struct xdfout_eegdev*)(dev_p))

#define DEFAULT_FILEPATH	"test.bdf"
#define CHUNK_NS	4

#define READ_STOP	0
#define READ_RUN	1
#define READ_EXIT	2


static const unsigned int dattab[EGD_NUM_DTYPE] = {
	[EGD_INT32] = XDFINT32,
	[EGD_FLOAT] = XDFFLOAT,
	[EGD_DOUBLE] = XDFDOUBLE,
};

static const char xdfout_device_type[] = "Data file";

static const char eegch_regex[] = "^("
	"(N|Fp|AF|F|FT|FC|A|T|C|TP|CP|P|PO|O|I)(z|[[:digit:]][[:digit:]]?)"
	"|([ABCDEF][[:digit:]][[:digit:]]?)"
	"|((EEG|[Ee]eg)[-:]?[[:digit:]]*)"
	")";

// Assume case insensitivity for this one
static const char trich_regex[] = 
	"^(status|tri(g(g(ers?)?)?)?)[-:]?[[:digit:]]*";

/******************************************************************
 *                  Internals implementation                      *
 ******************************************************************/
static void add_dtime_ns(struct timespec* ts, long delta_ns)
{
	ts->tv_nsec += delta_ns;
	if (ts->tv_nsec >= 1000000000) {
		ts->tv_nsec -= 1000000000;
		ts->tv_sec++;
	} else if (ts->tv_nsec < 0) {
		ts->tv_nsec += 1000000000;
		ts->tv_sec--;
	}
}


static
void extract_file_info(struct xdfout_eegdev* xdfdev, const char* filename)
{
	struct xdf* xdf = xdfdev->xdf;
	int nch, fs, i, stype;
	regex_t eegre, triggre;
	const char* label = NULL;
	struct systemcap cap = {.type_nch = {0}};

	xdf_get_conf(xdf, XDF_F_SAMPLING_FREQ, &fs,
			  XDF_F_NCHANNEL, &nch,
			  XDF_NOF);
	
	// Interpret the label to separate all channel type
	regcomp(&eegre, eegch_regex, REG_EXTENDED|REG_NOSUB);
	regcomp(&triggre, trich_regex, REG_EXTENDED|REG_NOSUB|REG_ICASE);
	for (i=0; i<nch; i++) {
		xdf_get_chconf(xdf_get_channel(xdf, i), 
				XDF_CF_LABEL, &label, XDF_NOF);
		stype = EGD_SENSOR;
		if (!regexec(&eegre, label, 0, NULL, 0)) 
			stype = EGD_EEG;
		else if (!regexec(&triggre, label, 0, NULL, 0))
			stype = EGD_TRIGGER;
		xdfdev->stypes[i] = stype;
		cap.type_nch[stype]++;
	}
	regfree(&triggre);
	regfree(&eegre);

	// Fill the capabilities metadata
	cap.sampling_freq = fs;
	cap.device_type = xdfout_device_type;
	cap.device_id = filename;
	xdfdev->dev.ci.set_cap(&xdfdev->dev, &cap);
}


static void* file_read_fn(void* arg)
{
	struct xdfout_eegdev* xdfdev = arg;
	struct xdf* xdf = xdfdev->xdf;
	const struct core_interface* restrict ci = &xdfdev->dev.ci;
	struct timespec next;
	void* chunkbuff = xdfdev->chunkbuff;
	pthread_mutex_t* runmtx = &(xdfdev->runmtx);
	pthread_cond_t* runcond = &(xdfdev->runcond);
	ssize_t ns;
	int runstate, ret, fs;

	clock_gettime(CLOCK_REALTIME, &next);
	xdf_get_conf(xdf, XDF_F_SAMPLING_FREQ, &fs, XDF_NOF);
	while (1) {
		// Wait for the runstate to be different from READ_STOP
		pthread_mutex_lock(runmtx);
		while ((runstate = xdfdev->runstate) == READ_STOP) {
			pthread_cond_wait(runcond, runmtx);
			memcpy(&next, &(xdfdev->start_ts), sizeof(next));
		}
		pthread_mutex_unlock(runmtx);
		if (runstate == READ_EXIT)
			break;

		// Schedule the next data chunk availability
		add_dtime_ns(&next, CHUNK_NS*(1000000000 / fs));
		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next, NULL);

		// Read the data chunk and update the eegdev accordingly
		ns = xdf_read(xdf, CHUNK_NS, chunkbuff);
		if (ns > 0)
			ret = ci->update_ringbuffer(&(xdfdev->dev),
				     chunkbuff, ns * xdfdev->dev.in_samlen);
		else {
			ci->report_error(&(xdfdev->dev), EAGAIN);
			ret = -1;
		}

		// Stop acquisition if something wrong happened
		if (ret) {
			pthread_mutex_lock(runmtx);
			if (xdfdev->runstate == READ_RUN)
				xdfdev->runstate = READ_STOP;
			pthread_mutex_unlock(runmtx);
		}
	}

	return NULL;
}


static int start_reading_thread(struct xdfout_eegdev* xdfdev)
{
	int ret;

	xdfdev->runstate = READ_STOP;
	
	if ( (ret = pthread_mutex_init(&(xdfdev->runmtx), NULL))
	    || (ret = pthread_cond_init(&(xdfdev->runcond), NULL))
	    || (ret = pthread_create(&(xdfdev->thread_id), NULL, 
	                             file_read_fn, xdfdev)) ) {
		errno = ret;
		return -1;
	}

	return 0;
}


static int stop_reading_thread(struct xdfout_eegdev* xdfdev)
{

	// Order the thread to stop
	pthread_mutex_lock(&(xdfdev->runmtx));
	xdfdev->runstate = READ_EXIT;
	pthread_cond_signal(&(xdfdev->runcond));
	pthread_mutex_unlock(&(xdfdev->runmtx));

	// Wait the thread to stop and free synchronization resources
	pthread_join(xdfdev->thread_id, NULL);
	pthread_cond_destroy(&(xdfdev->runcond));
	pthread_mutex_destroy(&(xdfdev->runmtx));
	return 0;
}


static unsigned int get_xdfch_index(const struct xdfout_eegdev* xdfdev,
				    int type, unsigned int index)
{
	unsigned int ich = 0, curr = 0;

	while (1) {
		if (xdfdev->stypes[ich] == type) {
			if (curr == index)
				return ich;
			curr++;
		}
		ich++;
	}
}


/******************************************************************
 *               XDF file out methods implementation              *
 ******************************************************************/
static
int xdfout_open_device(struct eegdev* dev, const char* optv[])
{
	struct xdf* xdf = NULL;
	void* chunkbuff = NULL;
	int nch, *stypes = NULL;
	size_t chunksize;
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	const char* filepath = dev->ci.getopt("path",DEFAULT_FILEPATH,optv);

	if (!(xdf = xdf_open(filepath, XDF_READ, XDF_ANY))) {
		if (errno == ENOENT)
			errno = ENODEV;
		goto error;
	}

	xdf_get_conf(xdf, XDF_F_NCHANNEL, &nch, XDF_NOF);
	chunksize = nch*sizeof(double)* CHUNK_NS;

	if (!(stypes = malloc(nch*sizeof(*stypes)))
	    || !(chunkbuff = malloc(chunksize)))
		goto error;

	// Initialize structures
	xdfdev->xdf = xdf;
	xdfdev->chunkbuff = chunkbuff;
	xdfdev->stypes = stypes;
	extract_file_info(xdfdev, filepath);

	// Start reading thread
	if (start_reading_thread(xdfdev))
		goto error;

	return 0;

error:
	if (xdf != NULL)
		xdf_close(xdf);
	free(chunkbuff);
	free(stypes);
	return -1;
}


static
int xdfout_close_device(struct eegdev* dev)
{
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	
	stop_reading_thread(xdfdev);

	xdf_close(xdfdev->xdf);
	free(xdfdev->chunkbuff);
	free(xdfdev->stypes);

	return 0;
}


static int xdfout_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	unsigned int i, j, ich, numch, type, dsize, offset = 0;
	size_t stride[1];
	struct selected_channels* selch;
	struct xdfch* ch;

	// Some channel may be unread: set default array index to nothing
	xdf_get_conf(xdfdev->xdf, XDF_F_NCHANNEL, &numch, XDF_NOF);
	for (j=0; j<numch; j++) {
		ch = xdf_get_channel(xdfdev->xdf, j);
		xdf_set_chconf(ch, XDF_CF_ARRINDEX, -1, XDF_NOF);
	}

	if (!(selch = dev->ci.alloc_input_groups(dev, ngrp)))
		return -1;

	for (i=0; i<ngrp; i++) {
		type = grp[i].datatype;
		dsize = egd_get_data_size(type);

		// Set parameters of (eeg -> ringbuffer)
		selch[i].in_offset = offset;
		selch[i].inlen = grp[i].nch * dsize;
		selch[i].typeout = selch[i].typein = type;
		selch[i].bsc = 0;
		selch[i].iarray = grp[i].iarray;
		selch[i].arr_offset = grp[i].arr_offset;

		// Set XDF channel configuration
		for (j=0; j<grp[i].nch; j++) {
			ich = get_xdfch_index(xdfdev, grp[i].sensortype, j);
			ch = xdf_get_channel(xdfdev->xdf, ich);
			xdf_set_chconf(ch, 
			               XDF_CF_ARRTYPE, dattab[type],
			               XDF_CF_ARRINDEX, 0,
				       XDF_CF_ARROFFSET, offset,
				       XDF_CF_ARRDIGITAL, 0,
				       XDF_NOF);
			offset += dsize;
		}
	}
	dev->ci.set_input_samlen(&(xdfdev->dev), offset);
	stride[0] = offset;
	xdf_define_arrays(xdfdev->xdf, 1, stride);
	xdf_prepare_transfer(xdfdev->xdf);
		
	return 0;
}


static int xdfout_start_acq(struct eegdev* dev)
{
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	pthread_mutex_lock(&(xdfdev->runmtx));

	xdf_seek(xdfdev->xdf, 0, SEEK_SET);
	memcpy(&(xdfdev->start_ts), &ts, sizeof(ts));

	xdfdev->runstate = READ_RUN;
	pthread_cond_signal(&(xdfdev->runcond));

	pthread_mutex_unlock(&(xdfdev->runmtx));

	return 0;
}


static int xdfout_stop_acq(struct eegdev* dev)
{
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	
	pthread_mutex_lock(&(xdfdev->runmtx));
	
	xdfdev->runstate = READ_STOP;
	pthread_cond_signal(&(xdfdev->runcond));
	
	pthread_mutex_unlock(&(xdfdev->runmtx));

	return 0;
}


static void xdfout_fill_chinfo(const struct eegdev* dev, int stype,
	                    unsigned int ich, struct egd_chinfo* info)
{
	unsigned int xdfind;
	const struct xdfch* ch;
	const struct xdfout_eegdev* xdfdev = get_xdf(dev);
	
	// Get target channel
	xdfind = get_xdfch_index(xdfdev, stype, ich);
	ch = xdf_get_channel(xdfdev->xdf, xdfind);
	
	// Fill channel information
	info->isint = (stype == EGD_TRIGGER) ? true : false;
	info->dtype = EGD_DOUBLE;
	xdf_get_chconf(ch, XDF_CF_PMIN, &(info->min.valdouble), 
		           XDF_CF_PMAX, &(info->max.valdouble),
	                   XDF_CF_LABEL, &(info->label),
			   XDF_CF_UNIT, &(info->unit),
			   XDF_CF_TRANSDUCTER, &(info->transducter),
			   XDF_CF_PREFILTERING, &(info->prefiltering),
		           XDF_NOF);
}


API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct xdfout_eegdev),
	.open_device = 		xdfout_open_device,
	.close_device = 	xdfout_close_device,
	.set_channel_groups = 	xdfout_set_channel_groups,
	.fill_chinfo = 		xdfout_fill_chinfo,
	.start_acq = 		xdfout_start_acq,
	.stop_acq = 		xdfout_stop_acq
};

