#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stddef.h>
#include <semaphore.h>
#include <usb.h>
#include <errno.h>
#include <string.h>

#include "eegdev-types.h"
#include "eegdev-common.h"

// It should ABSOLUTELY be a power of two or the read call will fail
#define CHUNKSIZE	(64*1024)

struct act2_eegdev {
	struct eegdev dev;
	sem_t hd_init;
	pthread_t thread_id;
	void* chunkbuff;
	usb_dev_handle* hudev;
	int runacq;
	unsigned char usb_data[64];
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

static const union scale act2_scales[EGD_NUM_DTYPE] = {
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
#define ACT2_TIMEOUT			1000

static int act2_read(usb_dev_handle* hdev, void* buff, size_t size)
{
	return usb_bulk_read(hdev, ACT2_EP_IN, buff, size, ACT2_TIMEOUT);
}


static int act2_write(usb_dev_handle* hdev, const void* buff, size_t size)
{
	return usb_bulk_write(hdev, ACT2_EP_OUT, buff, size, ACT2_TIMEOUT);
}


static usb_dev_handle* open_act2usbdev(struct usb_device *udev)
{
	usb_dev_handle* hdev;
	int interface, config;
	
	hdev = usb_open(udev);
	if (!hdev) 
		return NULL;

	config = udev->config[0].bConfigurationValue;
	interface = udev->config[0].interface[0].altsetting[0].bInterfaceNumber;

	// Specify settings
	if ( usb_set_configuration(hdev, config)
	   || usb_claim_interface(hdev, interface) ) {
		usb_close(hdev);
		hdev = NULL;
	}

	// TODO
	// Check that there are the expected endpoints

	return hdev;
}

static usb_dev_handle* act2_open_dev(void)
{
	struct usb_bus *busses;
	struct usb_bus *bus;
	struct usb_dev_handle *hdev = NULL;
	struct usb_device *udev = NULL;

	usb_init();

	// Poll all the usb devices
	usb_find_busses();
	usb_find_devices();
	busses = usb_get_busses();

	// Search for a device which has the expected Vendor and Product IDs
	for (bus = busses; (bus && !hdev); bus = bus->next) {
		for (udev = bus->devices; (udev && !hdev); udev = udev->next) {
			if ((udev->descriptor.idVendor == USB_ACTIVETWO_VENDOR_ID) &&
			    (udev->descriptor.idProduct == USB_ACTIVETWO_PRODUCT_ID)) {
				// Open the device, claim the interface and
				// do your processing
				hdev = open_act2usbdev(udev);
			}
		}
	}
	
	if (!hdev)
		errno = ENODEV;

	return hdev;
}


static int act2_close_dev(usb_dev_handle* hdev)
{
	int interface;
	struct usb_device* udev;

	if (hdev != NULL) {
		udev = usb_device(hdev);
		interface = udev->config[0].interface[0].altsetting[0].bInterfaceNumber;

		usb_release_interface(hdev, interface);
		usb_reset(hdev);
		usb_close(hdev);
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
	char* chunkbuff = a2dev->chunkbuff;
	ssize_t rsize = 0;
	int i, samstart, in_samlen, runacq;
	
	rsize = act2_read(a2dev->hudev, chunkbuff, CHUNKSIZE);
	if (rsize < 2 || !data_in_sync(chunkbuff)) {
		egd_report_error(&(a2dev->dev), (rsize < 2) ? EAGAIN : EIO);
		rsize = 0;
	} else
		act2_interpret_triggers(a2dev, ((uint32_t*)chunkbuff)[1]);
	
	// signals handshake has been enabled (or failed)
	sem_post(&(a2dev->hd_init));
	
	in_samlen = a2dev->dev.in_samlen;
	while (rsize) {
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
		rsize = act2_read(a2dev->hudev, chunkbuff, CHUNKSIZE);
		if (rsize < 0) {
			egd_report_error(&(a2dev->dev), EAGAIN);
			break;
		}
	}

	return NULL;
}


static int act2_disable_handshake(struct act2_eegdev* a2dev)
{
	unsigned char* usb_data = a2dev->usb_data;
	
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
	unsigned char* usb_data = a2dev->usb_data;
	int retval, error;

	// Init activetwo USB comm
	usb_data[0] = 0x00;
	act2_write(a2dev->hudev, usb_data, 64);

	// Start handshake
	usb_data[0] = 0xFF;
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


/******************************************************************
 *               Activetwo methods implementation                 *
 ******************************************************************/
API_EXPORTED
struct eegdev* egd_open_biosemi(void)
{
	struct act2_eegdev* a2dev = NULL;
	void* chunkbuff = NULL;
	usb_dev_handle* udev = NULL;

	// Open the USB device and alloc structure
	if ( !(udev = act2_open_dev())
	    || !(a2dev = malloc(sizeof(*a2dev)))
	    || !(chunkbuff = malloc(CHUNKSIZE)))
		goto error;

	// Initialize structures
	if (egd_init_eegdev(&(a2dev->dev), &biosemi_ops))
		goto error;
	a2dev->hudev = udev;
	a2dev->chunkbuff = chunkbuff;
	a2dev->runacq = 0;
	memset(a2dev->usb_data, 0, 64);
	sem_init(&(a2dev->hd_init), 0, 0);

	// Start the communication
	if (!act2_enable_handshake(a2dev))
		return &(a2dev->dev);

	//If we reach here, the communication has failed
	egd_destroy_eegdev(&(a2dev->dev));

error:
	act2_close_dev(udev);
	free(chunkbuff);
	free(a2dev);
	return NULL;
}


static int act2_close_device(struct eegdev* dev)
{
	struct act2_eegdev* a2dev = get_act2(dev);
	
	act2_disable_handshake(a2dev);
	egd_destroy_eegdev(dev);
	sem_destroy(&(a2dev->hd_init));

	act2_close_dev(a2dev->hudev);
	free(a2dev->chunkbuff);
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
