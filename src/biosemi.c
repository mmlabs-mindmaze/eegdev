#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stddef.h>
#include <semaphore.h>
#include <usb.h>
#include <errno.h>

#include "eegdev-common.h"

// It should ABSOLUTELY be a power of two (otherwise, the read call will fail)
#define CHUNKSIZE	(64*1024)

struct act2_eegdev {
	struct eegdev dev;
	sem_t hd_init;
	pthread_t thread_id;
	void* chunkbuff;
	usb_dev_handle* hudev;
};

#define get_act2(dev_p) \
	((struct act2_eegdev*)(((char*)(dev_p))-offsetof(struct act2_eegdev, dev)))

// Biosemi methods declaration
static int act2_close_device(struct eegdev* dev);
static int act2_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);

static const struct eegdev_operations biosemi_ops = {
	.close_device = act2_close_device,
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

static usb_dev_handle* act2_open_dev(void)
{
	struct usb_bus *busses;
	struct usb_bus *bus;
	struct usb_dev_handle *hdev = NULL;


	usb_init();

	// Poll all the usb devices
	usb_find_busses();
	usb_find_devices();
	busses = usb_get_busses();

	// Search for a device which has the expected Vendor and Product IDs
	for (bus = busses; (bus && !hdev); bus = bus->next) {
		struct usb_device *udev;
		for (udev = bus->devices; (udev && !hdev); udev = udev->next) {
			if ((udev->descriptor.idVendor == USB_ACTIVETWO_VENDOR_ID) &&
			    (udev->descriptor.idProduct == USB_ACTIVETWO_PRODUCT_ID)) {
				// Open the device, claim the interface and do your processing
				hdev = usb_open(udev);
				if (hdev) {
					int interface, config, retval;
					config = udev->config[0].bConfigurationValue;
					interface = udev->config[0].interface[0].altsetting[0].bInterfaceNumber;

					// Specify settings
					retval = usb_set_configuration(hdev, config);
					if (!retval)
						retval = usb_claim_interface(hdev, interface);

					// TODO
					// Check that there are the expected endpoints

					// Since the device is busy or does not work
					// we keep looking for a matching device
					if (retval) {
						usb_close(hdev);
						hdev = NULL;
						continue;
					}
				}
			}
		}
	}

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
static int act2_interpret_triggers(struct act2_eegdev* a2dev, uint32_t triggers)
{
	unsigned int sarray_size, speedmode, mk_model;

	// Determine speedmode
	speedmode = (triggers & 0x0E000000) >> 25;
	if (triggers & 0x20000000)
		speedmode += 8;

	// Determine model
	mk_model = (triggers & 0x80000000) ? 2 : 1;

	// Determine samplerate and the maximum number of EEG and sensor channels
	sarray_size = sample_array_sizes[mk_model-1][speedmode];
	a2dev->dev.cap.sampling_freq = samplerates[mk_model-1][speedmode];
	a2dev->dev.cap.eeg_nmax = num_eeg_channels[mk_model-1][speedmode];
	a2dev->dev.cap.sensor_nmax = sarray_size - a2dev->dev.cap.eeg_nmax - 2;
	a2dev->dev.cap.trigger_nmax = 1;

	a2dev->dev.in_samlen = sarray_size;

	return 0;
}


static void* multiple_sweeps_fn(void* arg)
{
	struct act2_eegdev* a2dev = arg;
	void* chunkbuff = a2dev->chunkbuff;
	ssize_t readsize;
	
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	if ((readsize = act2_read(a2dev->hudev, chunkbuff, CHUNKSIZE)) > 2)
		act2_interpret_triggers(a2dev, ((uint32_t*)chunkbuff)[1]);

	// signals handshake has been enabled (or failed)
	sem_post(&(a2dev->hd_init));

	while (readsize >= 0) {
		// Update the eegdev structure with the new data
		update_ringbuffer(&(a2dev->dev), chunkbuff, readsize);

		// Read data from the driver
		pthread_testcancel();
		readsize = act2_read(a2dev->hudev, chunkbuff, CHUNKSIZE);
	}

	pthread_exit(NULL);
}


static int act2_enable_handshake(struct act2_eegdev* a2dev)
{
	static unsigned char usb_data[64] = {0};
	int retval;

	// Init activetwo USB comm
	usb_data[0] = 0x00;
	act2_write(a2dev->hudev, usb_data, sizeof(usb_data));
	
	// Start reading from activetwo device
	retval = pthread_create(&(a2dev->thread_id), NULL, multiple_sweeps_fn, a2dev);
	if (retval) {
		errno = retval;
		return -1;
	}

	// Start handshake
	usb_data[0] = 0xFF;
	act2_write(a2dev->hudev, usb_data, sizeof(usb_data));

	// wait for the handshake completion
	sem_wait(&(a2dev->hd_init));
	

	return 0;
}


static int act2_disable_handshake(struct act2_eegdev* a2dev)
{
	static unsigned char usb_data[64] = {0};
	
	pthread_cancel(a2dev->thread_id);
	pthread_join(a2dev->thread_id, NULL);

	act2_write(a2dev->hudev, usb_data, sizeof(usb_data));
	return 0;
}


/******************************************************************
 *               Activetwo methods implementation                 *
 ******************************************************************/
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
	init_eegdev(&(a2dev->dev), &biosemi_ops);
	a2dev->hudev = udev;
	a2dev->chunkbuff = chunkbuff;
	sem_init(&(a2dev->hd_init), 0, 0);

	// Start the communication
	if (act2_enable_handshake(a2dev))
		goto error;

	return &(a2dev->dev);

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
	free(a2dev->chunkbuff);

	return 0;
}


static int act2_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	(void)dev;
	(void)ngrp;
	(void)grp;
	return -1;
}


