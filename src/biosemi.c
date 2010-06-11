#include <usb.h>

#include "eegdev-common.h"

#define READ_CHUNK_SIZE	(64*1024)

struct act2_eegdev {
	struct eegdev dev;
	usb_dev_handle* hudev;
};

#define get_act2(dev_p) \
	((struct act2_eegdev*)(((char*)(dev_p))-offsetof(struct act2_eegdev, dev)))

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
 *                       Activetwo implementation                 *
 ******************************************************************/

static int act2_close_device(struct eegdev* dev);
static int act2_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);
static const void* act2_update_data(struct eegdev* dev, ssize_t* len);

static const struct eegdev_operations biosemi_ops = {
	.close_device = act2_close_device,
	.set_channel_groups = act2_set_channel_groups,
	.update_data = act2_update_data,
};

struct eegdev* egd_open_biosemi(void)
{
	struct act2_eegdev* adev = NULL;
	usb_dev_handle* udev = NULL;
	struct eegdev_operations* ops;

	// Open the USB device and alloc structure
	if ( !(udev = act2_open_dev())
	    || !(adev = malloc(sizeof(*adev))))
		goto error;

	init_eegdev(&(adev->dev), &biosemi_ops);
	return &(adev->dev);

error:
	act2_close_dev(udev);
	free(adev);
	return NULL;
}


static int act2_enable_handshake(struct act2_eegdev* adev)
{
	static unsigned char usb_data[64] = {0};
	
	act2_write(adev->hudev, usb_data, sizeof(usb_data));
	usb_data[0] = 0xFF;
	act2_write(adev->hudev, usb_data, sizeof(usb_data));
//	act2_read(adev->hudev, );
	return 0;
}


static int act2_disable_handshake(struct act2_eegdev* adev)
{
	static unsigned char usb_data[64] = {0};
	
	act2_write(adev->hudev, usb_data, sizeof(usb_data));
	return 0;
}

static int act2_close_device(struct eegdev* dev)
{
	(void)dev;
}


static int act2_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	(void)dev;
	(void)ngrp;
	(void)grp;
}


const void* act2_update_data(struct eegdev* dev, ssize_t *len)
{
	(void)dev;
	(void)len;
	
	*len = -1;

	return NULL;
}

