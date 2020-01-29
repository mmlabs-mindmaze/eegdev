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

#include <mmerrno.h>
#include <mmlib.h>
#include <mmsysio.h>
#include <mmtime.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if DLOPEN_GUSBAMP
# include "gusbamp-loader.h"
#else
# include <gAPI.h>
#endif

#include <mmtime.h>
#include <stdint.h>

#include <eegdev-pluginapi.h>


#define ELT_NCH		17
#define ELT_SAMSIZE	(ELT_NCH*sizeof(float))
#define NUMELT_MAX	4
#define PREFILT_STR_SIZE	128
struct gtec_acq_element {
	char devname[16];
	void* buff;
	size_t bsize;
	int ielt;
};


struct gtec_eegdev {
	struct devmodule dev;
	int runacq;
	int buflen;
	void* buffer;

	// Master/Slave acquisition
	pthread_rwlock_t ms_lock;
	pthread_mutex_t bfulllock;
	pthread_cond_t bfullcond;
	unsigned int num_elt;
	struct gtec_acq_element elt[NUMELT_MAX];
	
	int fs;
	char devid[NUMELT_MAX*16];
	char prefiltering[PREFILT_STR_SIZE];
};

struct gtec_options
{
	double lp, hp, notch;
	const char* devid;
	unsigned int fs;
};

struct filtparam
{
	float order, fl, fh;
	int id;
};

#define get_gtec(dev_p) ((struct gtec_eegdev*)(dev_p))
#define get_elt_gtdev(dev_p) \
	((struct gtec_eegdev*)(((char*)(dev_p))-(offsetof(struct gtec_eegdev, elt)+((dev_p)->ielt * sizeof(struct gtec_acq_element)))))

/***************************************************************
 *                   Miscalleanous functions                   *
 **************************************************************/
static
void add_dtime_ns(struct mm_timespec* ts, long delta_ns)
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


/*****************************************************************
 *                        gtec metadata                          *
 *****************************************************************/
static const struct egdi_signal_info gtec_siginfo[2] = {
	{
		.isint = 0, .bsc = 0,
	  	.dtype = EGD_FLOAT, .mmtype = EGD_DOUBLE,
	  	.min.valdouble = -262144.0, .max.valdouble = 262143.96875,
	  	.unit = "uV", .transducer = "Active Electrode"
	}, {
		.isint = 1, .bsc = 0, 
	  	.dtype = EGD_FLOAT, .mmtype = EGD_INT32,
	  	.min.valint32_t = -8388608, .max.valint32_t = 8388607,
	  	.unit = "Boolean", .transducer = "Triggers and Status"
	}
};

enum {OPT_DEVID, OPT_HP, OPT_LP, OPT_NOTCH, OPT_FS, NUMOPT};
static const struct egdi_optname gtec_options[] = {
	[OPT_DEVID] = {.name = "deviceid", .defvalue = NULL},
	[OPT_HP] =    {.name = "highpass", .defvalue = "0.1"},
	[OPT_LP] =    {.name = "lowpasspass", .defvalue = "-1"},
	[OPT_NOTCH] = {.name = "notch", .defvalue = "50"},
	[OPT_FS] =    {.name = "samplerate", .defvalue = "512"},
	[NUMOPT] =    {.name = NULL}
};

static
const struct blockmapping gtec_mapping[2] = {
	{ .num_skipped = 16, .skipped_stype = EGD_EEG,
	     .default_info = &gtec_siginfo[0]},
	{ .num_skipped = 1, .skipped_stype = EGD_TRIGGER,
	     .default_info = &gtec_siginfo[1]}
};

/******************************************************************
 *                    open/close gTec device                      *
 ******************************************************************/
static
int gtec_open_devices(struct gtec_eegdev* gtdev, const char* devid)
{
	char **devlist, *chainid = gtdev->devid, devname[16];
	size_t dlen, slen = sizeof(gtdev->devid);
	const char* dname;
	unsigned int i = 0, j, numdev, len, ielt = 0;
	int error = 0, connected, anydev, opened;
	if (!devid)
		devid = "any";

	// Get the sorted list of connected gTec systems
	GT_UpdateDevices();
	numdev = GT_GetDeviceListSize();
	devlist = GT_GetDeviceList();

	len = strlen(devid);
	memset(chainid, '\0', slen);
	while (i < len) {
		// Parse next device element in the chain
		sscanf(devid+i, "%[^+]", devname);
		i += strlen(devname) + 1;
		if (ielt == NUMELT_MAX) {
			error = EMFILE;
			break;
		}

		// Check that device is connected and try to open it
		anydev = strcmp("any", devname) ? 0 : 1;
		connected = opened = 0;
		for (j=0; j<numdev && !opened; j++) {
			if (!anydev && strcmp(devname, devlist[j]))
				continue;
			dname = devlist[j];
			connected = !anydev;
			if (GT_OpenDevice(dname)) 
				opened = 1;
		}
			
		if (!opened) {
			error = connected ? EBUSY : ENODEV;
			break;
		}

		// Append device name to reported deviceid string
		snprintf(chainid, slen, "%s%s", ielt?"+":"", dname);
		dlen = strlen(chainid);
		slen -= dlen;
		chainid += dlen;

		gtdev->elt[ielt].ielt = ielt;
		strcpy(gtdev->elt[ielt++].devname, dname);
	}

	if (error) {
		for (i=0; i<ielt; i++)
			GT_CloseDevice(gtdev->elt[i].devname);
		errno = error;
	}
	gtdev->num_elt = ielt;
	GT_FreeDeviceList(devlist, numdev);
	return error ? -1 : 0;
}


static
void destroy_gtecdev(struct gtec_eegdev* gtdev)
{
	int i;

	// Close device starting by slaves
	for (i=gtdev->num_elt-1; i>=0; i--)
		GT_CloseDevice(gtdev->elt[i].devname);
}


/******************************************************************
 *                       gTec configuration                       *
 ******************************************************************/
static
float valabs(float f) {return (f >= 0.0f) ? f : -f;} //avoid include libm


static 
int gtec_find_bpfilter(const char* devname, gt_usbamp_config* conf,
                       float fl, float fh, float order,
		       struct filtparam* filtprm)
{
	float score, minscore = 1e12;
	int i, best = -1, nfilt;
	gt_size fs = conf->sample_rate;
	gt_filter_specification *filt = NULL;

	if ((fl == 0.0) && (fh == 0.0)) {
		filtprm->id = GT_FILTER_NONE;
		filtprm->order = 0;
		filtprm->fh = 0.0;
		filtprm->fl = 0.0;
		return 0;
	}

	// Get available filters
	nfilt = GT_GetBandpassFilterListSize(devname, fs);
	filt = malloc(nfilt*sizeof(*filt));
	if (filt == NULL)
		return -1;
	GT_GetBandpassFilterList(devname, fs, filt, nfilt*sizeof(*filt));
	
	// Test matching score of each filter
	for (i=0; i<nfilt; i++) {
		score = valabs(fl-filt[i].f_lower)/(fl < 1e-3 ? 1.0 : fl)
		       + valabs(fh-filt[i].f_upper)/(fh < 1e-3 ? 1.0 : fh)
		       + 1e-3*valabs(order-filt[i].order)/order;
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
int gtec_find_notchfilter(const char* devname, gt_usbamp_config* conf,
                          float freq, struct filtparam* filtprm)
{
	float score, minscore = 1e12;
	int i, best = -1, nfilt;
	gt_size fs = conf->sample_rate;
	gt_filter_specification *filt = NULL;

	if ((freq == 0.0)) {
		filtprm->id = GT_FILTER_NONE;
		filtprm->order = 0;
		filtprm->fh = 0.0;
		filtprm->fl = 0.0;
		return 0;
	}

	// Get available filters
	nfilt = GT_GetNotchFilterListSize(devname, fs);
	filt = malloc(nfilt*sizeof(*filt));
	if (filt == NULL)
		return -1;
	GT_GetNotchFilterList(devname, fs, filt, nfilt*sizeof(*filt));
	
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
	unsigned int i;
	struct blockmapping mappings[2*gtdev->num_elt];
	struct plugincap cap = {
		.num_mappings = 2*gtdev->num_elt,
		.mappings = mappings,
		.device_type = "gTec g.USBamp",
		.sampling_freq = gtdev->fs,
		.device_id = gtdev->devid,
		.flags = EGDCAP_NOCP_DEVID
	};
	struct devmodule* dev = &gtdev->dev;

	// Default conf: all systems provides EEG and 1 channel of trigger
	for (i=0; i < gtdev->num_elt; i++)
		memcpy(&mappings[2*i], gtec_mapping, sizeof(gtec_mapping));

	// Advertise capabilities
	dev->ci.set_cap(dev, &cap);
	dev->ci.set_input_samlen(dev, gtdev->num_elt*ELT_SAMSIZE);
}


static
int gtec_setup_conf(const char* devname, gt_usbamp_config* conf,
                    const struct gtec_options* gopt, char* filtstr)
{
	int i;
	char hpstr[16] = {0}, lpstr[16] = {0}, notchstr[32] = {0};
	struct filtparam bpprm, notchprm;

	conf->ao_config = NULL;
	conf->sample_rate = gopt->fs;
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
	if (gtec_find_bpfilter(devname, conf, gopt->hp, gopt->lp, 2, &bpprm)
	    || gtec_find_notchfilter(devname, conf, gopt->notch, &notchprm))
		return -1;
	
	// Setup prefiltering string
	if (bpprm.fl)
		snprintf(hpstr, sizeof(hpstr), "%.2f", bpprm.fl);
	else
		strcpy(hpstr, "DC");
	snprintf(lpstr, sizeof(lpstr), "%.1f",
	                      bpprm.fh ? bpprm.fh : 0.4*((double)gopt->fs));
	if (notchprm.id != GT_FILTER_NONE)
		snprintf(notchstr, sizeof(notchstr), "; Notch: %.1f Hz",
		                             0.5*(notchprm.fl+notchprm.fh));
	snprintf(filtstr, PREFILT_STR_SIZE,
	        "HP: %s Hz; LP: %s Hz%s", hpstr, lpstr, notchstr);

	// Set channel params
	for (i=0; i<GT_USBAMP_NUM_ANALOG_IN; i++) {
		conf->bandpass[i] = bpprm.id;
		conf->notch[i] = notchprm.id;
		conf->bipolar[i] = GT_BIPOLAR_DERIVATION_NONE;
		conf->analog_in_channel[i] = i+1;
	}
	return 0;
}


static
int gtec_configure_device(struct gtec_eegdev *gtdev,
                          const struct gtec_options* gopt)
{
	unsigned int i;
	const char* devname;
	gt_usbamp_config conf;
	gt_usbamp_asynchron_config as_conf = {
		.digital_out = {GT_FALSE, GT_FALSE, GT_FALSE, GT_FALSE}
	};
	gtdev->fs = gopt->fs;

	if (gtdev->num_elt == 0)
		return -1;
	
	for (i=0; i<gtdev->num_elt; i++) {
		devname = gtdev->elt[i].devname;
		gtec_setup_conf(devname, &conf, gopt, gtdev->prefiltering);

		// First device is master, the rest are slaves
		if (i!=0)
			conf.slave_mode = GT_TRUE;

		GT_SetConfiguration(devname, &conf);
		GT_SetAsynchronConfiguration(devname, &as_conf);
		GT_ApplyAsynchronConfiguration(devname);
	}

	gtec_setup_eegdev_core(gtdev);
	return 0;
}


static
void parse_gtec_options(const char* optv[], struct gtec_options* gopt)
{
	gopt->devid = optv[OPT_DEVID];
	gopt->fs = atoi(optv[OPT_FS]);

	if (!strcmp(optv[OPT_HP], "none"))
		gopt->hp = 0.0;
	else
		gopt->hp = atof(optv[OPT_HP]);
	if (!strcmp(optv[OPT_LP], "none"))
		gopt->lp = 0.0;
	else
		gopt->lp = atof(optv[OPT_LP]);
	if (!strcmp(optv[OPT_NOTCH], "none"))
		gopt->notch = 0.0;
	else
		gopt->notch = atof(optv[OPT_NOTCH]);
	
	if (gopt->lp < 0)
		gopt->lp = 0.4*((double)gopt->fs);
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
	const char* devname = gtdev->elt[0].devname;
	
	// Transfer data to ringbuffer by chunks of buflen bytes max
	sizetot = GT_GetSamplesAvailable(devname);
	while (sizetot > 0) {
		size = (sizetot < buflen) ? sizetot : buflen;
		size = GT_GetData(devname, buffer, size);
		if (size <= 0) {
			gtdev->dev.ci.report_error(&gtdev->dev, ENOMEM);
			return;
		}

		gtdev->dev.ci.update_ringbuffer(&gtdev->dev, buffer, size);
		sizetot -= size;
	}
}


static
size_t gtec_sync_buffer(struct gtec_acq_element* elt, size_t bsize)
{
	struct gtec_eegdev* gtdev = get_elt_gtdev(elt);
	const struct core_interface* restrict ci = &gtdev->dev.ci;
	pthread_rwlock_t* rwlock = &(gtdev->ms_lock);
	size_t minsize = SIZE_MAX, maxsize = 0;
	unsigned int i, nelt = gtdev->num_elt;
	pthread_mutex_t* bfulllock = &(gtdev->bfulllock);

	if (nelt == 0)
		return 0;
	
	elt->bsize = bsize;

	pthread_rwlock_unlock(rwlock);
	pthread_rwlock_wrlock(rwlock);

	// Calculate the smallest amount of data available in all element
	// i.e. what can be currently to the ringbuffer
	for (i=0; i<nelt; i++) {
		if (minsize > gtdev->elt[i].bsize)
			minsize = gtdev->elt[i].bsize;
	}

	// Send data to ringbuffer when all elements have some data
	if (minsize > 0) {
		char* buffer = gtdev->buffer;
		ci->update_ringbuffer(&(gtdev->dev), buffer, nelt*minsize);
		
		pthread_mutex_lock(bfulllock);

		// Empty from the common buffer the data sent
		for (i=0; i<nelt; i++) {
			if (maxsize < gtdev->elt[i].bsize)
				maxsize = gtdev->elt[i].bsize;
			gtdev->elt[i].bsize -= minsize;
		}
		if (minsize != maxsize)
			memmove(buffer, buffer+nelt*minsize, 
			                          nelt*(maxsize - minsize));

		// unblock element whose buffer was full
		if (maxsize == gtdev->buflen/nelt) 
			pthread_cond_broadcast(&(gtdev->bfullcond));
		
		pthread_mutex_unlock(bfulllock);
	}

	pthread_rwlock_unlock(rwlock);
	pthread_rwlock_rdlock(rwlock);

	return elt->bsize;
}


static
void gtec_callback_masterslave(void* data)
{
	struct gtec_acq_element* elt = data;
	struct gtec_eegdev* gtdev = get_elt_gtdev(elt);
	pthread_rwlock_t* rwlock = &(gtdev->ms_lock);
	unsigned char *ebuff = elt->buff, *rbuff = gtdev->buffer;
	unsigned int nelt = gtdev->num_elt, ielt = elt->ielt;
	ssize_t sizetot, i, pos, size = 0;
	ssize_t bsize, buflen = gtdev->buflen / nelt;
	const char* devname = elt->devname;
	int runacq;
	pthread_mutex_t* bfulllock = &(gtdev->bfulllock);

	// Block while the element buffer is full
	pthread_mutex_lock(bfulllock);
	while ((elt->bsize == (size_t)buflen) && gtdev->runacq)
		pthread_cond_wait(&(gtdev->bfullcond), bfulllock);
	runacq = gtdev->runacq; // to avoid *harmless* race condition
	pthread_mutex_unlock(bfulllock);
	if (!runacq)
		return;

	pthread_rwlock_rdlock(rwlock);
	bsize = elt->bsize;

	// Transfer data to ringbuffer by chunks of buflen bytes max
	sizetot = GT_GetSamplesAvailable(devname);
	while ((sizetot > 0) && (bsize < buflen)) {
		size = (sizetot < buflen-bsize) ? sizetot : buflen-bsize;
		size = GT_GetData(devname, ebuff, size);
		if (size <= 0) 
			break;

		// Update the common buffer with the new data
		for (i=0; i<size; i+=ELT_SAMSIZE) {
			pos = nelt*(i+bsize) + ielt*ELT_SAMSIZE;
			memcpy(rbuff+pos, ebuff+i, ELT_SAMSIZE);
		}
		bsize += size;
		
		// Synchronize with the other elements
		bsize = gtec_sync_buffer(elt, bsize);

		// Check that there is no recently added samples
		sizetot -= size;
		if (!sizetot)
			sizetot = GT_GetSamplesAvailable(devname);
	}

	pthread_rwlock_unlock(rwlock);

	if (size < 0)
		gtdev->dev.ci.report_error(&gtdev->dev, ENOMEM);
	else if (sizetot < 0)
		gtdev->dev.ci.report_error(&gtdev->dev, EIO);
}


static
int gtec_start_device_acq(struct gtec_eegdev* gtdev)
{
	int i, num = gtdev->num_elt;
	size_t eltbuflen;
	void (*cb)(void*);
	char* buff;
	void* arg;

	// prepare one buffer containing common buffer and element buffers
	eltbuflen = ELT_SAMSIZE*(size_t)(0.1 * (double)gtdev->fs);
	buff = malloc(2*num*eltbuflen);
	gtdev->runacq = 1;
	
	// Setup synchronization primitives
	pthread_mutex_init(&(gtdev->bfulllock), NULL);
	pthread_cond_init(&(gtdev->bfullcond), NULL);
	pthread_rwlock_init(&(gtdev->ms_lock), NULL);

	// Use the created buffer and set the acquisition callbacks
	gtdev->buflen = num*eltbuflen;
	gtdev->buffer = buff;
	cb = (num == 1) ? gtec_callback : gtec_callback_masterslave;
	for (i=0; i<num; i++) {
		arg = (num != 1) ? (void*) &(gtdev->elt[i]) : (void*)gtdev;
		GT_SetDataReadyCallBack(gtdev->elt[i].devname, cb, arg);
		gtdev->elt[i].buff =  buff + (num+i)*eltbuflen;
	}

	// Start device acquisition (starting by slaves)
	for (i=num-1; i>=0; i--) {
		// Wait between the last slave start and master start
		// in order to make sure that slave systems are ready
		// to receive the clock (200 ms should be enough)
		if ((num>1) && (i==0)) {
			struct mm_timespec ts;
			mm_gettime(CLOCK_REALTIME, &ts);
			add_dtime_ns(&ts, 200000000);
			mm_nanosleep(MM_CLK_REALTIME, &ts);
		}
		GT_StartAcquisition(gtdev->elt[i].devname);
	}
		
	return 0;
}


static
int gtec_stop_device_acq(struct gtec_eegdev* gtdev)
{
	int i;

	// unlock any stalled callback
	pthread_mutex_lock(&(gtdev->bfulllock));
	gtdev->runacq = 0;
	pthread_cond_broadcast(&(gtdev->bfullcond));
	pthread_mutex_unlock(&(gtdev->bfulllock));

	// Stop device acquisition (starting by slaves)
	for (i=gtdev->num_elt-1; i>=0; i--)
		GT_StopAcquisition(gtdev->elt[i].devname);

	// Clean the synchronisation primitives
	pthread_mutex_destroy(&(gtdev->bfulllock));
	pthread_cond_destroy(&(gtdev->bfullcond));
	pthread_rwlock_destroy(&(gtdev->ms_lock));

	// prepare small buffer
	free(gtdev->buffer);

	return 0;
}


/******************************************************************
 *                  gTec methods implementation                   *
 ******************************************************************/
static 
int gtec_close_device(struct devmodule* dev)
{
	struct gtec_eegdev* gtdev = get_gtec(dev);
	
	gtec_stop_device_acq(gtdev);
	destroy_gtecdev(gtdev);
#if DLOPEN_GUSBAMP
	unload_gusbamp_module();
#endif

	return 0;
}


static
int gtec_open_device(struct devmodule* dev, const char* optv[])
{
	struct gtec_options gopt;
	struct gtec_eegdev* gtdev = get_gtec(dev);

	parse_gtec_options(optv, &gopt);

#if DLOPEN_GUSBAMP
	if (load_gusbamp_module())
		return -1;
#endif

	if (gtec_open_devices(gtdev, gopt.devid)
	 || gtec_configure_device(gtdev, &gopt)
	 || gtec_start_device_acq(gtdev)) {
		// failure: clean up
		destroy_gtecdev(gtdev);
		return -1;
	}

	return 0;
}


API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct gtec_eegdev),
	.open_device = 		gtec_open_device,
	.close_device = 	gtec_close_device,
	.supported_opts =	gtec_options
};

