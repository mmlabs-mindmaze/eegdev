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
#include <stdio.h>

// Replacement declarations: it uses the proper declaration if the function
// is declared on the system
#include <portable-time.h>

#include <eegdev-pluginapi.h>

struct xdfout_eegdev {
	struct devmodule dev;
	pthread_t thread_id;
	pthread_cond_t runcond;
	pthread_mutex_t runmtx;
	int runstate;
	struct egdi_chinfo* chmap;
	void* chunkbuff;
	unsigned int in_samlen;
	size_t chunksize;
	struct xdf* xdf;
	struct timespec start_ts;
	int loop_file;
};

#define get_xdf(dev_p) ((struct xdfout_eegdev*)(dev_p))

#define CHUNK_NS	4

#define READ_STOP	0
#define READ_RUN	1
#define READ_EXIT	2

#define INFINITE_LOOP   -1


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
	"^(status|pixout|tri(g(g(ers?)?)?)?)[-:]?[[:digit:]]*";

static const struct egdi_optname xdfout_options[] = {
	{.name = "path", .defvalue = "test.bdf"},
	{.name = "loop", .defvalue = "no"},
	{.name = NULL}
};

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
	struct blockmapping mappings = {.num_skipped = 0};
	struct plugincap cap = {.num_mappings = 1, .mappings = &mappings};

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
		xdfdev->chmap[i].stype = stype;
		xdfdev->chmap[i].label = label;
		xdfdev->chmap[i].si = NULL;
	}
	regfree(&triggre);
	regfree(&eegre);

	// Fill the capabilities metadata
	mappings.nch = nch;
	mappings.chmap = xdfdev->chmap;
	cap.sampling_freq = fs;
	cap.device_type = xdfout_device_type;
	cap.device_id = filename;
	cap.flags = EGDCAP_NOCP_CHMAP | EGDCAP_NOCP_CHLABEL
	                                              | EGDCAP_NOCP_DEVTYPE;
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
		// Rewing if end of file reached and file must be looped
		ns = xdf_read(xdf, CHUNK_NS, chunkbuff);
		if (ns == 0 && xdfdev->loop_file) {
			xdf_seek(xdf, 0, SEEK_SET);
			ns = xdf_read(xdf, CHUNK_NS, chunkbuff);
			if (xdfdev->loop_file != INFINITE_LOOP)
				xdfdev->loop_file--;
		}

		if (ns > 0)
			ret = ci->update_ringbuffer(&(xdfdev->dev),
				     chunkbuff, ns * xdfdev->in_samlen);
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
		if (xdfdev->chmap[ich].stype == type) {
			if (curr == index)
				return ich;
			curr++;
		}
		ich++;
	}
}


/**
 * parse_loop_option_value() - parse value of "loop" option
 * @val:        string passed to option
 *
 * This function parse @val which is supposed to hold the string value of
 * the loop option. If support the following value:
 *  - "yes", "true" => INFINITE_LOOP
 *  - "no", "false" => 0
 *  - <n>           => n (where n is positive)
 *
 * If @val hold an unrecognized value, 0 is returned.
 *
 * Return: the number of loop that must be perform, INFINITE_LOOP if it
 * should never stop (for "yes" or "true" values).
 */
static
int parse_loop_option_value(const char* val)
{
	int num, prev_err;
	char* endptr;

	if (  strcmp(val, "yes") == 0
	   || strcmp(val, "true") == 0)
		return INFINITE_LOOP;

	if (  strcmp(val, "no") == 0
	   || strcmp(val, "false") == 0)
		return 0;

	prev_err = errno;
	num = strtol(val, &endptr, 10);
	if (*endptr != '\0' || num < 0)
		num = 0;
	errno = prev_err;

	return num;
}

/******************************************************************
 *               XDF file out methods implementation              *
 ******************************************************************/
static
int xdfout_open_device(struct devmodule* dev, const char* optv[])
{
	struct xdf* xdf = NULL;
	void* chunkbuff = NULL;
	int nch;
	struct egdi_chinfo* chmap = NULL;
	size_t chunksize;
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	const char* filepath = optv[0];
	const char* loop_optval = optv[1];

	if (!(xdf = xdf_open(filepath, XDF_READ, XDF_ANY))) {
		if (errno == ENOENT)
			errno = ENODEV;
		goto error;
	}

	xdf_get_conf(xdf, XDF_F_NCHANNEL, &nch, XDF_NOF);
	chunksize = nch*sizeof(double)* CHUNK_NS;

	if (!(chmap = malloc(nch*sizeof(*chmap)))
	    || !(chunkbuff = malloc(chunksize)))
		goto error;

	// Initialize structures
	xdfdev->xdf = xdf;
	xdfdev->chunkbuff = chunkbuff;
	xdfdev->chmap = chmap;
	xdfdev->loop_file = parse_loop_option_value(loop_optval);
	extract_file_info(xdfdev, filepath);

	// Start reading thread
	if (start_reading_thread(xdfdev))
		goto error;

	return 0;

error:
	if (xdf != NULL)
		xdf_close(xdf);
	free(chunkbuff);
	free(chmap);
	return -1;
}


static
int xdfout_close_device(struct devmodule* dev)
{
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	
	stop_reading_thread(xdfdev);

	xdf_close(xdfdev->xdf);
	free(xdfdev->chunkbuff);
	free(xdfdev->chmap);

	return 0;
}


static int xdfout_set_channel_groups(struct devmodule* dev, unsigned int ngrp,
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
			ich = get_xdfch_index(xdfdev, grp[i].sensortype, j+grp[i].index);
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
	xdfdev->in_samlen = offset;
	stride[0] = offset;
	xdf_define_arrays(xdfdev->xdf, 1, stride);
	xdf_prepare_transfer(xdfdev->xdf);
		
	return 0;
}


static int xdfout_start_acq(struct devmodule* dev)
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


static int xdfout_stop_acq(struct devmodule* dev)
{
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	
	pthread_mutex_lock(&(xdfdev->runmtx));
	
	xdfdev->runstate = READ_STOP;
	pthread_cond_signal(&(xdfdev->runcond));
	
	pthread_mutex_unlock(&(xdfdev->runmtx));

	return 0;
}


static void xdfout_fill_chinfo(const struct devmodule* dev, int stype,
	                       unsigned int ich, struct egdi_chinfo* info,
			       struct egdi_signal_info* si)
{
	unsigned int xdfind;
	const struct xdfch* ch;
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	
	// Get target channel
	xdfind = get_xdfch_index(xdfdev, stype, ich);
	ch = xdf_get_channel(xdfdev->xdf, xdfind);
	
	// Fill channel information
	si->isint = (stype == EGD_TRIGGER) ? 1 : 0;
	si->mmtype = EGD_DOUBLE;
	xdf_get_chconf(ch, XDF_CF_PMIN, &(si->min.valdouble), 
		           XDF_CF_PMAX, &(si->max.valdouble),
	                   XDF_CF_LABEL, &(info->label),
			   XDF_CF_UNIT, &(si->unit),
			   XDF_CF_TRANSDUCTER, &(si->transducer),
			   XDF_CF_PREFILTERING, &(si->prefiltering),
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
	.stop_acq = 		xdfout_stop_acq,
	.supported_opts =	xdfout_options
};

