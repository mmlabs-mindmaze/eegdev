#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <semaphore.h>
#include <libusb-1.0/libusb.h>
#include "usb_comm.h"

#include "eegdev-types.h"
#include "eegdev-common.h"

// It should ABSOLUTELY be a power of two or the read call will fail
#define CHUNKSIZE	(64*1024)

struct act2_eegdev {
	struct eegdev dev;
	sem_t hd_init;
	pthread_t thread_id;
	int runacq;

	// USB communication related
	libusb_device_handle* hudev;
	struct usb_btransfer ubtr;
};

#define get_act2(dev_p) \
	((struct act2_eegdev*)(((char*)(dev_p))-offsetof(struct act2_eegdev, dev)))

#define data_in_sync(pdata)	(*((const uint32_t*)(pdata)) == 0xFFFFFF00)

// Biosemi methods declaration
static int act2_close_device(struct eegdev* dev);
static int act2_noaction(struct eegdev* dev);
static int act2_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);

static const struct eegdev_operations biosemi_ops = {
	.close_device = act2_close_device,
	.start_acq = act2_noaction,
	.stop_acq = act2_noaction,
	.set_channel_groups = act2_set_channel_groups,
};


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
	[EGD_INT32] = {.i32val = 1},
	[EGD_FLOAT] = {.fval = (1.0f/8192.0f)},
	[EGD_DOUBLE] = {.dval = (1.0/8192.0)},
};


/******************************************************************
 *                       USB interaction                          *
 ******************************************************************/
#define USB_ACTIVETWO_VENDOR_ID		0x0547
#define USB_ACTIVETWO_PRODUCT_ID	0x1005
#define ACT2_EP_OUT			0x01
#define ACT2_EP_IN			0x82
#define ACT2_TIMEOUT			200

static int act2_write(libusb_device_handle* hudev, void* buff, size_t size)
{
	int actual_length, ret;
	ret = libusb_bulk_transfer(hudev, ACT2_EP_OUT, buff, 
	                           size, &actual_length, ACT2_TIMEOUT);
	if (ret) {
		errno = ENODEV;
		return -1;
	}
	return actual_length;
}


static libusb_device_handle* act2_open_dev(void)
{
	libusb_device_handle *hudev;
	hudev = libusb_open_device_with_vid_pid(egd_acquire_usb_context(),
					       USB_ACTIVETWO_VENDOR_ID,
					       USB_ACTIVETWO_PRODUCT_ID);
	if (!hudev) {
		egd_release_usb_context();
		errno = ENODEV;
	}
	return hudev;
}


static int act2_close_dev(libusb_device_handle* hudev)
{
	if (hudev != NULL) {
		libusb_close(hudev);
		egd_release_usb_context();
	}
	return 0;
}



/******************************************************************
 *                       Activetwo internals                      *
 ******************************************************************/
static int act2_interpret_triggers(struct act2_eegdev* a2dev, uint32_t tri)
{
	unsigned int arr_size, speedmode, mk_model;

	// Determine speedmode
	speedmode = (tri & 0x0E000000) >> 25;
	if (tri & 0x20000000)
		speedmode += 8;

	// Determine model
	mk_model = (tri & 0x80000000) ? 2 : 1;

	// Determine sampling frequency and the maximum number of EEG and
	// sensor channels
	arr_size = sample_array_sizes[mk_model-1][speedmode];
	a2dev->dev.cap.sampling_freq = samplerates[mk_model-1][speedmode];
	a2dev->dev.cap.eeg_nmax = num_eeg_channels[mk_model-1][speedmode];
	a2dev->dev.cap.sensor_nmax = arr_size - a2dev->dev.cap.eeg_nmax - 2;
	a2dev->dev.cap.trigger_nmax = 1;

	a2dev->dev.in_samlen = arr_size*sizeof(int32_t);

	return 0;
}


static void* multiple_sweeps_fn(void* arg)
{
	struct act2_eegdev* a2dev = arg;
	struct usb_btransfer* ubtr = &(a2dev->ubtr);
	char* chunkbuff = NULL;
	ssize_t rsize = 0;
	int i, samstart, in_samlen, runacq;
	
	if (egd_start_usb_btransfer(ubtr))
		goto endinit;
		
	rsize = egd_swap_usb_btransfer(ubtr, &chunkbuff);
	if (rsize < 2 || !data_in_sync(chunkbuff)) {
		egd_report_error(&(a2dev->dev), (rsize < 0) ? errno : EIO);
		goto endinit;
	}
	act2_interpret_triggers(a2dev, ((uint32_t*)chunkbuff)[1]);
	
endinit:
	// signals handshake has been enabled (or failed)
	sem_post(&(a2dev->hd_init));
	
	in_samlen = a2dev->dev.in_samlen;
	while (rsize > 0) {
		pthread_mutex_lock(&(a2dev->dev.synclock));
		runacq = a2dev->runacq;
		pthread_mutex_unlock(&(a2dev->dev.synclock));
		if (!runacq)
			break;

		// check presence synchro code
		samstart = (in_samlen - a2dev->dev.in_offset) % in_samlen;
		for (i=samstart; i<rsize; i+=in_samlen) {
			if (!data_in_sync(chunkbuff+i)) {
				egd_report_error(&(a2dev->dev), EIO);
				return NULL;
			}
		}

		// Update the eegdev structure with the new data
		if (egd_update_ringbuffer(&(a2dev->dev), chunkbuff, rsize))
			break;

		// Read data from the USB device
		rsize = egd_swap_usb_btransfer(ubtr, &chunkbuff);
		if (rsize < 0) {
			egd_report_error(&(a2dev->dev), errno);
			break;
		}
	}

	egd_stop_usb_btransfer(ubtr);
	return NULL;
}


static int act2_disable_handshake(struct act2_eegdev* a2dev)
{
	unsigned char usb_data[64] = {0};
	
	//pthread_cancel(a2dev->thread_id);
	pthread_mutex_lock(&(a2dev->dev.synclock));
	a2dev->runacq = 0;
	pthread_mutex_unlock(&(a2dev->dev.synclock));
	
	pthread_join(a2dev->thread_id, NULL);

	usb_data[0] = 0x00;
	act2_write(a2dev->hudev, usb_data, 64);
	return 0;
}



static int act2_enable_handshake(struct act2_eegdev* a2dev)
{
	unsigned char usb_data[64] = {0};
	int retval, error;

	// Init activetwo USB comm
	usb_data[0] = 0x00;
	act2_write(a2dev->hudev, usb_data, 64);

	// Start reading from activetwo device
	a2dev->runacq = 1;
	retval = pthread_create(&(a2dev->thread_id), NULL,
	                        multiple_sweeps_fn, a2dev);
	if (retval) {
		a2dev->runacq = 0;
		errno = retval;
		return -1;
	}

	// Start handshake
	usb_data[0] = 0xFF;
	act2_write(a2dev->hudev, usb_data, 64);
	

	// wait for the handshake completion
	sem_wait(&(a2dev->hd_init));

	// Check that handshake has been enabled
	pthread_mutex_lock(&(a2dev->dev.synclock));
	error = a2dev->dev.error;
	pthread_mutex_unlock(&(a2dev->dev.synclock));
	if (error) {
		act2_disable_handshake(a2dev);	
		errno = error;
		return -1;
	}

	return 0;
}


static int init_act2dev(struct act2_eegdev* a2dev)
{
 	libusb_device_handle* hudev = NULL;
	
	if (!(hudev = act2_open_dev()))
		return -1;
		
 	if (egd_init_eegdev(&(a2dev->dev), &biosemi_ops))
		goto error;

	if (egd_init_usb_btransfer(&(a2dev->ubtr), hudev, ACT2_EP_IN,
				   CHUNKSIZE, ACT2_TIMEOUT))
		goto error2;
	
	sem_init(&(a2dev->hd_init), 0, 0);
	a2dev->runacq = 0;
	a2dev->hudev = hudev;
	return 0;

error2:
	egd_destroy_eegdev(&(a2dev->dev));
error:
	act2_close_dev(hudev);
	return -1; 
}


static void destroy_act2dev(struct act2_eegdev* a2dev)
{
	if (a2dev == NULL)
		return;

	sem_destroy(&(a2dev->hd_init));
	egd_destroy_usb_btransfer(&(a2dev->ubtr));
	egd_destroy_eegdev(&(a2dev->dev));
	act2_close_dev(a2dev->hudev);
}


/******************************************************************
 *               Activetwo methods implementation                 *
 ******************************************************************/
API_EXPORTED
struct eegdev* egd_open_biosemi(void)
{
	struct act2_eegdev* a2dev = NULL;

	// alloc and initialize tructure
	if ( !(a2dev = malloc(sizeof(*a2dev)))
	    || init_act2dev(a2dev) )
		goto error;

	// Start the communication
	if (!act2_enable_handshake(a2dev))
		return &(a2dev->dev);

	//If we reach here, the communication has failed
	destroy_act2dev(a2dev);

error:
	free(a2dev);
	return NULL;
}


static int act2_close_device(struct eegdev* dev)
{
	struct act2_eegdev* a2dev = get_act2(dev);
	
	act2_disable_handshake(a2dev);
	destroy_act2dev(a2dev);
	free(a2dev);

	return 0;
}


static int act2_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	unsigned int i, stype;
	struct selected_channels* selch = dev->selch;
	unsigned int offsets[EGD_NUM_STYPE] = {
		[EGD_EEG] = 2*sizeof(int32_t),
		[EGD_TRIGGER] = sizeof(int32_t)+1,
		[EGD_SENSOR] = (2+dev->cap.eeg_nmax)*sizeof(int32_t),
	};
	
	for (i=0; i<ngrp; i++) {
		stype = grp[i].sensortype;

		// Set parameters of (eeg -> ringbuffer)
		selch[i].in_offset = offsets[stype]
		                     + grp[i].index*sizeof(int32_t);
		selch[i].len = grp[i].nch*sizeof(int32_t);
		selch[i].cast_fn = egd_get_cast_fn(EGD_INT32, grp[i].datatype, 
					  (stype == EGD_TRIGGER) ? 0 : 1);
		selch[i].sc = act2_scales[grp[i].datatype];
	}
		
	return 0;
}


static int act2_noaction(struct eegdev* dev)
{
	(void)dev;
	return 0;
}
