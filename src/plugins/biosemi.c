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
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <pthread.h>
#include <byteswap.h>
#include <libusb.h>

#include <eegdev-pluginapi.h>

#ifndef le_to_cpu_32
# if WORDS_BIGENDIAN
#  define le_to_cpu_u32(data)	bswap_32(data)
# else
#  define le_to_cpu_u32(data)	(data)
# endif //WORDS_BIGENDIAN
#endif

// It should ABSOLUTELY be a power of two or the read call will fail
#define CHUNKSIZE	(64*1024)
#define NUMURB		2


struct act2_eegdev {
	struct devmodule dev;

	char prefiltering[32];
	//unsigned int nch;

	int samplelen;	//number of int32 in a time sample
	int inoffset;	//offset in next chunk of a sample (in num of int32)

	// USB communication related
	pthread_t thread_id;
	pthread_cond_t cond;
	pthread_mutex_t mtx;
	int stopusb, resubmit, num_running;
	libusb_context* ctx;
	libusb_device_handle* hudev;
	struct libusb_transfer* urb[NUMURB];
};


#define get_act2(dev_p) ((struct act2_eegdev*)(((char*)(dev_p))))

/*****************************************************************
 *                     System capabilities                       *
 *****************************************************************/
static const unsigned short samplerates[2][9] = {
	{2048, 4096, 8192, 16384, 2048, 4096, 8192, 16384, 2048},
	{2048, 2048, 2048, 2048, 2048, 4096, 8192, 16384, 2048}
};
static const unsigned short sample_array_sizes[2][9] = {
	{258, 130, 66, 34, 258, 130, 66, 34, 290}, 
	{610, 610, 610, 610, 282, 154, 90, 58, 314}
}; 
static const unsigned short num_eeg_channels[2][9] = {
	{256, 128, 64, 32, 232, 104, 40, 8, 256}, 
	{512, 512, 512, 512, 256, 128, 64, 32, 280}
}; 

static const union gval act2_scales[EGD_NUM_DTYPE] = {
	[EGD_INT32] = {.valint32_t = 1},
	[EGD_FLOAT] = {.valfloat = (1.0f/8192.0f)},
	[EGD_DOUBLE] = {.valdouble = (1.0/8192.0)},
};

static const char trigg_prefiltering[] = "No filtering";
static const char trigglabel[] = "Status";
static const char device_id[] = "N/A";
static const struct egdi_signal_info act2_siginfo[2] = {
	{
		.isint = 0, .bsc = 1, .scale = 1.0/8192.0,
		.dtype = EGD_INT32, .mmtype = EGD_DOUBLE,
		.min.valdouble = -262144.0, .max.valdouble = 262143.96875,
		.unit = "uV", .transducer = "Active Electrode"
	}, {
		.isint = 1, .bsc = 0,
		.dtype = EGD_INT32, .mmtype = EGD_INT32,
		.min.valint32_t = -8388608, .max.valint32_t = 8388607,
		.unit = "Boolean", .transducer = "Triggers and Status"
	}
};

enum {OPT_EEGMAP, OPT_SENSMAP, NOPT};
static const struct egdi_optname act2_options[] = {
	[OPT_EEGMAP] = {.name = "eegmap", .defvalue = NULL},
	[OPT_SENSMAP] = {.name = "sensormap", .defvalue = NULL},
	[NOPT] = {.name = NULL}
};

/******************************************************************
 *                       USB interaction                          *
 ******************************************************************/
#define USB_ACTIVETWO_VENDOR_ID		0x0547
#define USB_ACTIVETWO_PRODUCT_ID	0x1005
#define ACT2_EP_OUT			0x01
#define ACT2_EP_IN			0x82
#define ACT2_TIMEOUT			200

static
int proc_libusb_error(int libusbret)
{
	if (libusbret == 0)
		return 0;
	else if (libusbret == LIBUSB_ERROR_TIMEOUT)
		return EAGAIN;
	else if (libusbret == LIBUSB_ERROR_BUSY)
		return EBUSY;
	else if (libusbret == LIBUSB_ERROR_NO_DEVICE)
		return ENODEV;
	else 
		return EIO;
}

static
int proc_libusb_transfer_ret(int ret)
{
	switch (ret) {
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_CANCELLED:
		return 0;

	case LIBUSB_TRANSFER_TIMED_OUT:
		return EAGAIN;

	case LIBUSB_TRANSFER_NO_DEVICE:
		return ENODEV;

	case LIBUSB_TRANSFER_ERROR:
	case LIBUSB_TRANSFER_OVERFLOW:
	case LIBUSB_TRANSFER_STALL:
	default:
		return EIO;
	}
}


static void* usb_event_handling_proc(void* arg)
{
	struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
	struct act2_eegdev* a2dev = arg;
	int quit;

	while (1) {
		pthread_mutex_lock(&a2dev->mtx);
		quit = a2dev->stopusb;
		pthread_mutex_unlock(&a2dev->mtx);
		if (quit)
			break;

		libusb_handle_events_timeout(a2dev->ctx, &tv);
	}

	return NULL;
}


static int act2_write(libusb_device_handle* hudev, void* buff, size_t size)
{
	int actual_length, ret;
	ret = libusb_bulk_transfer(hudev, ACT2_EP_OUT, buff, 
	                           size, &actual_length, ACT2_TIMEOUT);
	if (ret) {
		errno = proc_libusb_error(ret);
		return -1;
	}
	return actual_length;
}


static int act2_open_dev(struct act2_eegdev* a2dev)
{
	libusb_device_handle *hudev = NULL;
	libusb_context* ctx = NULL;
	int ret, errnum;

	// Initialize a session to libusb, open an Activetwo2 device
	// and initialize the endpoints
	if ( (ret = libusb_init(&ctx))
	   || !(hudev = libusb_open_device_with_vid_pid(ctx,
					       USB_ACTIVETWO_VENDOR_ID,
					       USB_ACTIVETWO_PRODUCT_ID))
	   || (ret = libusb_set_configuration(hudev, 1))
	   || (ret = libusb_claim_interface(hudev, 0))
	   || (ret = libusb_clear_halt(hudev, ACT2_EP_OUT)) 
	   || (ret = libusb_clear_halt(hudev, ACT2_EP_IN)) )
		goto error;

	a2dev->ctx = ctx;
	a2dev->hudev = hudev;
	
	if (pthread_cond_init(&a2dev->cond, NULL)
	  || pthread_mutex_init(&a2dev->mtx, NULL)
	  || pthread_create(&a2dev->thread_id, NULL,
	                    usb_event_handling_proc, a2dev))
		goto error;
	
	return 0;


error:
	if (hudev)
		libusb_close(hudev);
	if (ctx)
		libusb_exit(ctx);
	errnum = proc_libusb_error(ret);
	errno = errnum ? errnum : EIO;
	return -1;
}


static int act2_close_dev(struct act2_eegdev* a2dev)
{
	// Close the USB device
	if (a2dev->hudev != NULL) {
		libusb_release_interface(a2dev->hudev, 0);
		libusb_close(a2dev->hudev);
	}

	// Close the session to libusb
	if (a2dev->ctx) {
		pthread_mutex_lock(&a2dev->mtx);
		a2dev->stopusb = 1;
		pthread_mutex_unlock(&a2dev->mtx);
		pthread_join(a2dev->thread_id, NULL);
		pthread_mutex_destroy(&a2dev->mtx);
		pthread_cond_destroy(&a2dev->cond);
		libusb_exit(a2dev->ctx);
	}
	return 0;
}



/******************************************************************
 *                       Activetwo internals                      *
 ******************************************************************/
static
int setup_channel_map(struct act2_eegdev* a2dev, int arrlen, int neeg,
                      struct egdi_chinfo *chmap, const char* optv[])
{
	int i, nch, nsens = arrlen-2-neeg;
	const struct egdi_chinfo *map;
	struct devmodule* dev = &a2dev->dev;

	for (i=0; i<arrlen; i++) {
		chmap[i].label = NULL;
		chmap[i].stype = -1;
	}

	chmap[1].stype = EGD_TRIGGER;
	chmap[1].label = trigglabel;

	// Get EEG mapping
	map = dev->ci.get_conf_mapping(dev, optv[OPT_EEGMAP], &nch);
	if (map) {
		nch = (nch <= neeg) ? nch : neeg;
		memcpy(chmap+2, map, nch*sizeof(*map));
	} else
		for (i=0; i<neeg; i++)
			chmap[i+2].stype = EGD_EEG;

	// Get sensor mapping
	map = dev->ci.get_conf_mapping(dev, optv[OPT_SENSMAP], &nch);
	if (map) {
		nch = (nch <= nsens) ? nch : nsens;
		memcpy(chmap+2+neeg, map, nch*sizeof(*map));
	} else
		for (i=0; i<nsens; i++)
			chmap[i+2+neeg].stype = EGD_SENSOR;

	// Inform about the incoming data type
	chmap[0].si = &act2_siginfo[1];
	chmap[1].si = &act2_siginfo[1];
	for (i=2; i<arrlen; i++)
		chmap[i].si = &act2_siginfo[0];

	return 0;
}


static
int parse_triggers(struct act2_eegdev* a2dev, uint32_t tri,
                                              const char* optv[])
{
	char devtype[128];
	unsigned int arr_size, mode, mk, eeg_nmax;
	struct systemcap cap;
	int samlen;
	struct egdi_chinfo* tmp_chmap;
	struct devmodule* dev = &a2dev->dev;

	// Determine speedmode
	mode = (tri & 0x0E000000) >> 25;
	if (tri & 0x20000000)
		mode += 8;

	// Determine model
	mk = (tri & 0x80000000) ? 2 : 1;

	// Determine sampling frequency and the maximum number of EEG and
	// sensor channels
	arr_size = sample_array_sizes[mk-1][mode];
	eeg_nmax = num_eeg_channels[mk-1][mode];
	a2dev->samplelen = arr_size;

	if ( !(tmp_chmap = malloc(arr_size*sizeof(*tmp_chmap)))
	  || setup_channel_map(a2dev, arr_size, eeg_nmax, tmp_chmap, optv) )
		return -1;

	// Set the capabilities
	sprintf(devtype, "Biosemi ActiveTwo Mk%u", mk+1);
	cap.device_type = devtype;
	cap.device_id = device_id;
	cap.sampling_freq = samplerates[mk-1][mode];
	cap.chmap = tmp_chmap;
	cap.nch = arr_size;
	cap.flags = EGDCAP_NOCP_DEVID;
	dev->ci.set_cap(dev, &cap);

	free(tmp_chmap);

	// Fill the prefiltering field
	snprintf(a2dev->prefiltering, sizeof(a2dev->prefiltering),
	        "HP: DC; LP: %.1f Hz", (double)(cap.sampling_freq/4.9112));

	samlen = arr_size*sizeof(int32_t);
	dev->ci.set_input_samlen(dev, samlen);
	return 0;
}


static
void process_usbbuf(struct act2_eegdev* a2dev, uint32_t* buf, ssize_t bs)
{
	int i, start, slen = a2dev->samplelen, inoffset = a2dev->inoffset;
	const struct core_interface* ci = &(a2dev->dev.ci);

#if WORDS_BIGENDIAN
	for (i=0; i<bs; i++)
		buf[i] = bswap_32(buf[i]);
#endif //WORDS_BIGENDIAN

	// check presence synchro code and shift trigger value         	
	start = (slen - inoffset) % slen;
	for (i=start; i<bs; i+=slen) {
		if (buf[i] != 0xFFFFFF00) {
			ci->report_error(&(a2dev->dev), EIO);
			return;
		}
		buf[i+1] >>= 8;
	}
	a2dev->inoffset = (inoffset + bs)%slen;

	// Update the eegdev structure with the new data
	ci->update_ringbuffer(&(a2dev->dev), buf, bs*sizeof(*buf));
}
                                                              	
                                                              	
#ifndef LIBUSB_CALL                                           	
#define LIBUSB_CALL                                           	
#endif                                                        	
static void LIBUSB_CALL req_completion_fn(struct libusb_transfer *transfer)
{                                                                     
	int ret, requeue = 1;
	struct act2_eegdev* a2dev = transfer->user_data;
	const struct core_interface* ci = &(a2dev->dev.ci);

	// interpret the USB buffer content and update the ringbuffer
	if (transfer->actual_length)
		process_usbbuf(a2dev, (uint32_t*)transfer->buffer,
		               transfer->actual_length/sizeof(uint32_t));

	// Check that no error occured
	if ((ret = proc_libusb_transfer_ret(transfer->status))) {
		ci->report_error(&(a2dev->dev), ret);
		requeue = 0;
	}
	
	pthread_mutex_lock(&a2dev->mtx);

	// requeue again the chunk buffer if still running
	requeue = a2dev->resubmit ? requeue : 0;
	if (requeue && (ret = libusb_submit_transfer(transfer))) {
		ci->report_error(&(a2dev->dev), proc_libusb_error(ret));
		requeue = 0;
	}

	// Signal main thread that this urb stopped
	if (!requeue) {
		a2dev->num_running--;
		pthread_cond_signal(&a2dev->cond);
	}

	pthread_mutex_unlock(&a2dev->mtx);
}


static int act2_disable_handshake(struct act2_eegdev* a2dev)
{
	int i;
	unsigned char usb_data[64] = {0};
	
	// Notify the Active2 hardware to stop acquiring data
	usb_data[0] = 0x00;
	act2_write(a2dev->hudev, usb_data, 64);

	// Notify urb to cancel and wait for them to actually finish
	pthread_mutex_lock(&a2dev->mtx);
	a2dev->resubmit = 0;
	for (i=0; i<NUMURB; i++)
		libusb_cancel_transfer(a2dev->urb[i]);
	
	while (a2dev->num_running)
		pthread_cond_wait(&a2dev->cond, &a2dev->mtx);
	pthread_mutex_unlock(&a2dev->mtx);

	return 0;
}



static
int act2_enable_handshake(struct act2_eegdev* a2dev, const char* optv[])
{
	unsigned char usb_data[64] = {0};
	uint32_t* buf = (uint32_t*) a2dev->urb[0]->buffer;
	int transferred, ret, i;

	// Init activetwo USB comm
	usb_data[0] = 0x00;
	act2_write(a2dev->hudev, usb_data, 64);

	// Start handshake
	usb_data[0] = 0xFF;
	act2_write(a2dev->hudev, usb_data, 64);

	// Transfer the first chunk of data and check the first synchro
	ret = libusb_bulk_transfer(a2dev->hudev, ACT2_EP_IN, 
	                           (unsigned char*)buf, CHUNKSIZE,
		                   &transferred, ACT2_TIMEOUT);
	ret = proc_libusb_error(ret);
	if (ret || (transferred < 2)
	   || (le_to_cpu_u32(buf[0])!=0xFFFFFF00)) {
		errno = ret ? ret : EIO;
		return -1;
	}

	// Parse the first trigger to get info about the system and transfer
	// the buffer into the ringbuffer
	parse_triggers(a2dev, le_to_cpu_u32(buf[1]), optv);
	process_usbbuf(a2dev, buf, transferred/sizeof(*buf));

	// Submit all the URB in advance in order to queue them into the
	// USB host controller
	pthread_mutex_lock(&a2dev->mtx);
	a2dev->resubmit = 1;
	for (i=0; i<NUMURB; i++) {
		if ((ret = libusb_submit_transfer(a2dev->urb[i]))) {
			pthread_mutex_unlock(&a2dev->mtx);
			errno = proc_libusb_error(ret);
			act2_disable_handshake(a2dev);
			return -1;
		}
		a2dev->num_running++;
	}
	pthread_mutex_unlock(&a2dev->mtx);

	return 0;
}


static void* page_aligned_malloc(size_t len)
{
#if HAVE_POSIX_MEMALIGN && HAVE_SYSCONF
	int ret;
	void* memptr;
	size_t pgsize = sysconf(_SC_PAGESIZE);
	if ((ret = posix_memalign(&memptr, pgsize, len))) {
		errno = ret;
		return NULL;
	}
	else
		return memptr;
#else
	return malloc(len);
#endif
}


static int init_act2dev(struct act2_eegdev* a2dev)
{
 	int i;
	
	if (act2_open_dev(a2dev))
		return -1;
		
	// Initialize the asynchronous USB bulk transfer 
	for (i=0; i<NUMURB; i++) {
		if (!(a2dev->urb[i] = libusb_alloc_transfer(0))
		  ||!(a2dev->urb[i]->buffer=page_aligned_malloc(CHUNKSIZE)))
			goto error;

		libusb_fill_bulk_transfer(a2dev->urb[i],
		                          a2dev->hudev, ACT2_EP_IN,
		                          a2dev->urb[i]->buffer, CHUNKSIZE,
		                          req_completion_fn, a2dev,
		                          ACT2_TIMEOUT);
	}
	
	return 0;

error:
	for (i=0; i<NUMURB; i++) {
		if (a2dev->urb[i]) {
			free(a2dev->urb[i]->buffer);
			libusb_free_transfer(a2dev->urb[i]);
		}
	}
	act2_close_dev(a2dev);
	return -1; 
}


static void destroy_act2dev(struct act2_eegdev* a2dev)
{
	int i;

	if (a2dev == NULL)
		return;

	for (i=0; i<NUMURB; i++) {
		free(a2dev->urb[i]->buffer);
		libusb_free_transfer(a2dev->urb[i]);
	}
	act2_close_dev(a2dev);
}


/******************************************************************
 *               Activetwo methods implementation                 *
 ******************************************************************/
static
int act2_open_device(struct devmodule* dev, const char* optv[])
{
	struct act2_eegdev* a2dev = get_act2(dev);

	// alloc and initialize tructure
	if (init_act2dev(a2dev))
		return -1;

	// Start the communication
	if (act2_enable_handshake(a2dev, optv)) {
		destroy_act2dev(a2dev);
		return -1;
	}

	return 0;
}


static int act2_close_device(struct devmodule* dev)
{
	struct act2_eegdev* a2dev = get_act2(dev);
	
	act2_disable_handshake(a2dev);
	destroy_act2dev(a2dev);

	return 0;
}


API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct act2_eegdev),
	.open_device = 		act2_open_device,
	.close_device = 	act2_close_device,
	.supported_opts = 	act2_options
};


