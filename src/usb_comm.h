#ifndef USB_COMM_H
#define USB_COMM_H

#include <libusb-1.0/libusb.h>

/***********************************************************************
 *                     libusb context handling                         *
 ***********************************************************************/
/* Acquires an USB context. The context will be created if it is called
 * for the first time.
 * A context must be acquired before using opening a USB device
 *
 * returns a pointer to the context
 */
LOCAL_FN libusb_context* egd_acquire_usb_context(void);


/* Releases the USB context. If it is the last release, it will be
 * destroyed.
 * The context must be released after the USB device is closed.
 */
LOCAL_FN void egd_release_usb_context(void);



/***********************************************************************
 *                 fast buffered transfer functions                    *
 ***********************************************************************/
#define NCHUNKBUFF	3
struct usb_btransfer {
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	int iurb, iread;
	void* chunkbuff[NCHUNKBUFF];
	int actual_length[NCHUNKBUFF];
	int state;	// O normal, > 0 error, -1 < cancelled/stopped
	int stop;
	struct libusb_transfer* urb;
};

/* \param ubtr	pointer to a structure usb_btransfer
 * \param hudev	handle to a USB device
 * \param ep	endpoint to wich the request should be sent
 * \param len	size in bytes of each individual buffers
 * \param to_ms	timeout in ms
 *
 * Initialize the buffered transfer.
 *
 * returns 0 in case of success, -1 otherwise and errno is set accordingly
 */
LOCAL_FN int egd_init_usb_btransfer(struct usb_btransfer* ubtr, 
	                            libusb_device_handle* hudev, int ep,
                                    size_t len, unsigned int to_ms);


/* \param ubtr	pointer to an initialized usb_btransfer structure
 *
 * Destroy a usb_btransfer that has been previously initialized
 */
LOCAL_FN void egd_destroy_usb_btransfer(struct usb_btransfer* ubtr);


/* \param ubtr	pointer to an initialized usb_btransfer structure
 *
 * Start the buffered transfer
 */
LOCAL_FN int egd_start_usb_btransfer(struct usb_btransfer* ubtr);


/* \param ubtr	pointer to an initialized usb_btransfer structure
 *
 * Stop the buffered transfer taht has been previously started
 */
LOCAL_FN void egd_stop_usb_btransfer(struct usb_btransfer* ubtr);


/* \param ubtr	pointer to an initialized usb_btransfer structure
 * \param buff	pointer to the pointer that will receive the data
 *
 * Receives the pointer to the next chunk of data
 * 
 * returns 0 in case of success, -1 otherwise and errno is set accordingly
 */
LOCAL_FN int egd_swap_usb_btransfer(struct usb_btransfer* ubtr, char** buff);

#endif /* USB_COMM_H*/
