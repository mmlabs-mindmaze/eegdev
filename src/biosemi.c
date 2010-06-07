#include <usb.h>

#include "eegdev-common.h"

#define READ_CHUNK_SIZE	(64*1024)

struct act2_eegdev {
	struct eegdev dev;
	usb_dev_handle* hudev;
};

#define get_act2(dev_p) \
	((struct act2_eegdev*)(((char*)(dev_p))-offsetof(struct act2_eegdev, dev)))

/******************************************************************
 *                       USB interaction                          *
 ******************************************************************/
#define USB_ACTIVETWO_VENDOR_ID		0x0547
#define USB_ACTIVETWO_PRODUCT_ID	0x1005
#define ACTIVETWO_ENDPOINT_OUT		0x01
#define ACTIVETWO_ENDPOINT_IN		0x82
#define ACT2_TIMEOUT			1000

static int read_act2_dev(usb_dev_handle* hdev, void* buffer, size_t size)
{
	return usb_bulk_read(hdev, ACTIVETWO_ENDPOINT_IN, buffer, size, ACT2_TIMEOUT);
}

static int write_act2_dev(usb_dev_handle* hdev, const void* buffer, size_t size)
{
	return usb_bulk_write(hdev, ACTIVETWO_ENDPOINT_OUT, buffer, size, ACT2_TIMEOUT);
}

static usb_dev_handle* open_act2_dev(void)
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


static int close_act2_dev(usb_dev_handle* hdev)
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

static void act2_close_device(struct eegdev* dev);
static void act2_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);
static int act2_update_data(struct eegdev* dev);

struct eegdev* egd_open_biosemi(void)
{
	struct act2_eegdev* adev = NULL;
	usb_dev_handle* udev = NULL;

	// Open the USB device and alloc structure
	if ( !(udev = open_act2_dev())
	    || !(adev = malloc(sizeof(*adev))))
		goto error;

	// Setup device methods
	adev->close_device = act2_close_device;
	adev->set_channel_groups = act2_set_channel_groups;
	adev->update_data = act2_update_data;

	return &(adev->dev);

error:
	close_act2_dev(udev);
	free(adev);
	return NULL;
}

static int act2_start_communication(struct act2_eegdev* adev)
{
	static unsigned char usb_data[64] = {0};
	
	write_act2_dev(adev->hudev, usb_data, sizeof(usb_data));
	write_act2_dev(adev->hudev, usb_data, sizeof(usb_data));
}
