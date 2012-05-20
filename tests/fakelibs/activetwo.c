/*
    Copyright (C) 2011-2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <limits.h>
#include <libusb.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <byteswap.h>

#include "fakeact2.h"
#include "time-utils.h"

// Replacement declarations: it uses the proper declaration if the function
// is declared on the system
#include <portable-time.h>

#ifndef LIBUSB_CALL
#define LIBUSB_CALL
#endif

#define USB_ACTIVETWO_VENDOR_ID		0x0547
#define USB_ACTIVETWO_PRODUCT_ID	0x1005
#define ACT2_EP_OUT			0x01
#define ACT2_EP_IN			0x82

#define MAXREQ	8

struct usb_request {
	struct libusb_transfer* transfer;
	struct timespec ts;
};

struct event_queue {
	pthread_cond_t cond;
	pthread_mutex_t lock;

	int nqueued, free;
	struct usb_request queue[MAXREQ];
};


struct libusb_context {
	struct event_queue queue;
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

/********************************************
 *                 event queue              *
 ********************************************/
static
void init_queue(struct event_queue* queue)
{
	memset(queue, 0, sizeof(*queue));

	pthread_cond_init(&queue->cond, NULL);
	pthread_mutex_init(&queue->lock, NULL);
}


static
void destroy_queue(struct event_queue* queue, pthread_t thid)
{
	pthread_mutex_lock(&queue->lock);
	queue->free = 1;
	pthread_cond_signal(&queue->cond);
	pthread_mutex_unlock(&queue->lock);

	if (thid)
		pthread_join(thid, NULL);

	pthread_cond_destroy(&queue->cond);	
	pthread_mutex_destroy(&queue->lock);	
}


static
struct libusb_transfer* peek_transfer(struct event_queue* eq, int req)
{
	struct libusb_transfer* transfer = NULL;
	
	if ((req < 0) || (req >= eq->nqueued))
		return NULL;

	transfer = eq->queue[req].transfer;
	eq->nqueued--;
	memmove(&eq->queue[req], &eq->queue[req+1], 
	        eq->nqueued * sizeof(eq->queue[0]));

	return transfer;
}


static 
int wait_transfer(struct event_queue* eq,
                  int (*ts_cb)(struct timespec*, void*), void* data)
{
	struct timespec ts, nextts = {.tv_sec = eq->queue[0].ts.tv_sec + (LONG_MAX/1000000)};
	int i, req = -1;
	unsigned int timeout;

	while (1) {
		if (eq->free) 
			return -1;

		// Check when the normal operation will succeed
		if (eq->nqueued && ts_cb)
			req = ts_cb(&nextts, data);
	
		// Find the next event timestamp
		for (i=0; i<eq->nqueued; i++) {
			// Handle cancelled transfer with higher priority
			if (eq->queue[i].transfer->status == LIBUSB_TRANSFER_CANCELLED) 
				return i;
				
			// Test timeout timestamp
			memcpy(&ts, &eq->queue[i].ts, sizeof(ts));
			timeout = eq->queue[i].transfer->timeout;
			addtime(&ts, timeout/1000, (timeout%1000)*1000000);
			if (difftime_us(&ts, &nextts) < 0) {
				req = i;
				memcpy(&nextts, &ts, sizeof(ts));
			}
		}

		// Wait for the timeout or for a request to be added
		if (req < 0)
			pthread_cond_wait(&eq->cond, &eq->lock);
		else if (pthread_cond_timedwait(&eq->cond, &eq->lock, &nextts) == ETIMEDOUT)
			break;
	}

	return req;
}


static 
int enqueue_transfer(struct event_queue* eq, struct libusb_transfer* transfer, struct timespec* ts)
{
	int i;
	
	pthread_mutex_lock(&eq->lock);
	if (eq->free)
		goto exit;
	i = eq->nqueued++;
	eq->queue[i].transfer = transfer;
	memcpy(&eq->queue[i].ts, ts, sizeof(*ts));
	pthread_cond_signal(&eq->cond);
exit:
	pthread_mutex_unlock(&eq->lock);

	return 0;
}


static 
int cancel_transfer(struct event_queue* eq, struct libusb_transfer* transfer)
{
	int i, ret = -1;
	pthread_mutex_lock(&eq->lock);
	for (i=0; i<eq->nqueued; i++) {
		if ((eq->queue[i].transfer == transfer)
		 && (transfer->status != LIBUSB_TRANSFER_CANCELLED)) {
			transfer->status = LIBUSB_TRANSFER_CANCELLED;
			ret = 0;
			pthread_cond_signal(&eq->cond);
		}
	}
	pthread_mutex_unlock(&eq->lock);

	return ret;
}


static 
struct libusb_transfer* dequeue_transfer(struct event_queue* eq, struct timespec* to)
{
	struct libusb_transfer* transfer;

	pthread_mutex_lock(&eq->lock);
	while (to && !eq->nqueued) {
		if (pthread_cond_timedwait(&eq->cond, &eq->lock, to) == ETIMEDOUT)
			break;
	}
	transfer = peek_transfer(eq, 0);
	pthread_mutex_unlock(&eq->lock);

	return transfer;
}


/*************************************************************
 *                    Device implementation                  *
 *************************************************************/
struct libusb_device_handle {
	int streaming;
	int32_t stateval;
	struct timespec start;
	int quit;
	int fs, samsize, datatransfer;
	unsigned int neeg, nexg;
	struct libusb_context* ctx;
	struct event_queue ep_in, ep_out;
	pthread_t th_ep_in, th_ep_out;
};

static
void fill_data_buffer(struct libusb_device_handle* dev,
                      unsigned char* data, size_t datatr, unsigned int len)
{
	unsigned int i, trlen, soff, son, arrlen;
	unsigned int neeg = dev->neeg, nexg = dev->nexg;
	int32_t *sdata = (int32_t*)data, stateval = dev->stateval;
	size_t is = datatr / dev->samsize;
	arrlen = dev->samsize/sizeof(*sdata);
	len /= sizeof(*sdata);
	son = (datatr % dev->samsize)/sizeof(*sdata);
	soff = arrlen;
	trlen = (soff - son);

	while (len) {
		if (trlen > len) {
			soff -= trlen - len;
			trlen = len;
		}

		if (son == 0 && soff >= 1) 
			sdata[0-son] = 0xFFFFFF00;
		
		if (son <= 1 && soff >= 2)
			sdata[1-son] = compute_trigger(is, stateval);

		for (i=0; i<neeg; i++)
			if (i+2>=son && i+2<soff)
				sdata[2+i-son] = get_analog_val(is, i, 0);

		for (i=0; i<nexg; i++)
			if (i+2+neeg>=son && i+2+neeg<soff)
				sdata[2+neeg+i-son] = get_analog_val(is, i, 1);

#if WORDS_BIGENDIAN
		for (i=0; i<trlen; i++)
			sdata[i] = bswap_32(sdata[i]);
#endif

		is++;
		len -= trlen;
		sdata += trlen;
		son = 0;
		trlen = soff = arrlen;
	}

}


static
void init_stateval(struct libusb_device_handle* dev, unsigned int mode, unsigned int mk)
{
	uint32_t stateval;

	dev->samsize = sample_array_sizes[mk][mode] * sizeof(int32_t);
	dev->fs = samplerates[mk][mode];
	dev->neeg = num_eeg_channels[mk][mode];
	dev->nexg = dev->samsize/sizeof(int32_t) - dev->neeg - 2;

	stateval = 0x00000000;
	stateval |= ((mode & 0x7)<<25) | ((mode & 0x8)<< 29);
	stateval |= (1 << 28); //CMS in range
	stateval |= (mk << 31);

	memcpy(&dev->stateval, &stateval, sizeof(stateval));

}


static
void next_ts(struct libusb_device_handle* dev, struct timespec* ts, int len)
{
	int nsample, nsec, size, fs = dev->fs;

	size = dev->datatransfer + len;
	nsample = (size / dev->samsize) + (size % dev->samsize ? 1 : 0);

	memcpy(ts, &dev->start, sizeof(*ts));
	nsec = (nsample%fs)*1.0e9/((double)fs);
	addtime(ts, nsample/fs, nsec);
}


static
int ts_in(struct timespec* ts, void* data)
{
	struct libusb_device_handle* dev = data;
	struct libusb_transfer* transfer = dev->ep_in.queue[0].transfer;

	if (!dev->streaming)
		return -1;

	next_ts(dev, ts, transfer->length);
	return 0;
}


static
int get_available(struct libusb_device_handle* dev)
{
	struct timespec ts;
	int usec, nsample, data, streaming;

	clock_gettime(CLOCK_REALTIME, &ts);
	
	streaming = dev->streaming;
	usec = difftime_us(&ts, &dev->start);
	nsample = (int)(((double)dev->fs)*(1.0e-6*(double)usec));
	data = nsample * dev->samsize - dev->datatransfer;
	
	return streaming ? data - data%64 : 0;
}


static
void* endpoint_in_fn(void* data)
{
	struct libusb_device_handle* dev = data;	
	struct libusb_context* ctx = dev->ctx;
	struct libusb_transfer* transfer;
	struct event_queue* eq = &dev->ep_in;
	struct timespec ts = {0, 0};
	int trlen, reqlen, req, offset;
	int status;

	pthread_mutex_lock(&eq->lock);
	while ((req = wait_transfer(eq, ts_in, dev)) >= 0) {
		if (req == 0) {
			trlen = get_available(dev);
			reqlen = eq->queue[0].transfer->length;
			trlen = (trlen < reqlen) ? trlen : reqlen;
			trlen = trlen - trlen%64;
		} else
			trlen = 0;

		offset = dev->datatransfer;
		dev->datatransfer += trlen;
		transfer = peek_transfer(eq, req);

		pthread_mutex_unlock(&eq->lock);

		transfer->actual_length = trlen;
		status = transfer->status;
		if (status != LIBUSB_TRANSFER_CANCELLED) 
			status = trlen ?  LIBUSB_TRANSFER_COMPLETED
			                : LIBUSB_TRANSFER_TIMED_OUT;
		transfer->status = status;

		fill_data_buffer(dev, transfer->buffer, offset, trlen);

		// Process transfer
		enqueue_transfer(&ctx->queue, transfer, &ts);

		pthread_mutex_lock(&eq->lock);
	}
	pthread_mutex_unlock(&eq->lock);

	return NULL;
}


static
int ts_out(struct timespec* ts, void* data)
{
	struct event_queue* eq = data;
	
	if (!eq->nqueued)
		return -1;

	memcpy(ts, &eq->queue[0].ts, sizeof(*ts));
	return 0;
}




static
void* endpoint_out_fn(void* data)
{
	struct libusb_device_handle* dev = data;
	struct libusb_context* ctx = dev->ctx;
	struct libusb_transfer* transfer;
	struct event_queue* eq = &dev->ep_out;
	struct timespec ts = {0, 0};
	int req;

	pthread_mutex_lock(&eq->lock);
	while ((req = wait_transfer(eq, ts_out, &dev->ep_out))>=0) {
		transfer = peek_transfer(eq, req);
		pthread_mutex_unlock(&eq->lock);

		transfer->status = LIBUSB_TRANSFER_COMPLETED;
		transfer->actual_length = transfer->length;
		
		pthread_mutex_lock(&dev->ep_in.lock);
		if (transfer->buffer[0] == 0x00)
			dev->streaming = 0;
		else if (transfer->buffer[0] == 0xFF) {
			dev->streaming = 1;
			clock_gettime(CLOCK_REALTIME, &dev->start);
		}
		pthread_cond_signal(&dev->ep_in.cond);
		pthread_mutex_unlock(&dev->ep_in.lock);

		// Return transfer in libusb context
		enqueue_transfer(&ctx->queue, transfer, &ts);

		pthread_mutex_lock(&eq->lock);
	}
	pthread_mutex_unlock(&eq->lock);

	return NULL;
}


static
void init_device(struct libusb_device_handle* dev, struct libusb_context* ctx)
{
	dev->ctx = ctx;
	dev->streaming = 0;

	init_stateval(dev, 4, 1);

	init_queue(&dev->ep_in);
	init_queue(&dev->ep_out);

	pthread_create(&dev->th_ep_in, NULL, endpoint_in_fn, dev);
	pthread_create(&dev->th_ep_out, NULL, endpoint_out_fn, dev);
}


static
void destroy_device(struct libusb_device_handle* dev)
{
	destroy_queue(&dev->ep_in, dev->th_ep_in);
	destroy_queue(&dev->ep_out, dev->th_ep_out);
}


/*************************************************************
 *                                                           *
 *************************************************************/
LIBUSB_CALL
int libusb_init(libusb_context **context)
{
	struct libusb_context* ctx;
	ctx = calloc(1,sizeof(*ctx));

	init_queue(&ctx->queue);

	*context = ctx;
	return 0;
}


LIBUSB_CALL
void libusb_exit(libusb_context *ctx)
{
	destroy_queue(&ctx->queue, 0);
	free(ctx);
}


LIBUSB_CALL
int libusb_handle_events_timeout(libusb_context *ctx, struct timeval *tv)
{
	int free_xfer;
	struct libusb_transfer* xfer = NULL;
	struct timespec tots, curr, *to = NULL;

	// Setup timeout timestamp
	if (tv) {
		clock_gettime(CLOCK_REALTIME, &curr);
		memcpy(&tots, &curr, sizeof(curr));
		addtime(&tots, tv->tv_sec, tv->tv_usec*1000);
		to = &tots;
	}

	// Get transfer one by one
	while ((xfer = dequeue_transfer(&ctx->queue, to))) {
		to = NULL;
		free_xfer = xfer->flags & LIBUSB_TRANSFER_FREE_TRANSFER;
		xfer->callback(xfer);
		if (free_xfer)
			libusb_free_transfer(xfer);
	}

	return 0;
}


LIBUSB_CALL
int libusb_submit_transfer(struct libusb_transfer *transfer)
{
	struct timespec ts;
	libusb_device_handle *dev = transfer->dev_handle;
	struct event_queue *eq;
	clock_gettime(CLOCK_REALTIME, &ts);
	addtime(&ts, 0, 10000000);
	
	if (transfer->endpoint == ACT2_EP_IN)
		eq = &dev->ep_in;
	else if (transfer->endpoint == ACT2_EP_OUT)
		eq = &dev->ep_out;
	else
		return LIBUSB_ERROR_OTHER;

	enqueue_transfer(eq, transfer, &ts);	

	return 0;
}


LIBUSB_CALL
int libusb_clear_halt(libusb_device_handle *dev, unsigned char endpoint)
{
	(void)dev;
	(void)endpoint;
	return 0;
}


LIBUSB_CALL
int libusb_cancel_transfer(struct libusb_transfer *transfer)
{
	libusb_device_handle *dev = transfer->dev_handle;
	struct event_queue *eq;
	
	if (transfer->endpoint == ACT2_EP_IN)
		eq = &dev->ep_in;
	else if (transfer->endpoint == ACT2_EP_OUT)
		eq = &dev->ep_out;
	else
		return LIBUSB_ERROR_OTHER;
	
	if (cancel_transfer(eq, transfer))
		return LIBUSB_ERROR_NOT_FOUND;

	return 0;
}


LIBUSB_CALL
struct libusb_transfer *libusb_alloc_transfer(int iso_packets)
{
	struct libusb_transfer* transfer;

	transfer = calloc(1, sizeof(*transfer)
	                + iso_packets*sizeof(transfer->iso_packet_desc[0]));
	if (transfer) 
		transfer->num_iso_packets = iso_packets;
	return transfer;
}


LIBUSB_CALL
void libusb_free_transfer(struct libusb_transfer *transfer)
{
	if (transfer->flags & LIBUSB_TRANSFER_FREE_BUFFER)
		free(transfer->buffer);
	free(transfer);
}


struct sync_data {
	int done, actual_length, status;
	pthread_cond_t cond;
	pthread_mutex_t lock;
};


static LIBUSB_CALL
void sync_transfer_cb(struct libusb_transfer *transfer)
{
	struct sync_data* data = transfer->user_data;
	
	data->actual_length = transfer->actual_length;
	data->status = transfer->status;

	pthread_mutex_lock(&data->lock);
	data->done = 1;
	pthread_cond_signal(&data->cond);
	pthread_mutex_unlock(&data->lock);
}


LIBUSB_CALL
int libusb_bulk_transfer(libusb_device_handle *dev,
	unsigned char endpoint, unsigned char *data, int length,
	int *actual_length, unsigned int timeout)
{
	int retval = 0;
	struct sync_data* user_data;
	struct libusb_transfer* xfer;

	// Initialize sychronization primitives
	user_data = calloc(1, sizeof(*user_data));
	pthread_mutex_init(&user_data->lock, NULL);
	pthread_cond_init(&user_data->cond, NULL);

	// Submit asynchronous transfer
	xfer = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(xfer, dev, endpoint, data, length,
	                          sync_transfer_cb, user_data, timeout);
	libusb_submit_transfer(xfer);

	// wait for completion
	pthread_mutex_lock(&user_data->lock);
	while (!user_data->done)
		pthread_cond_wait(&user_data->cond, &user_data->lock);
	pthread_mutex_unlock(&user_data->lock);
	
	*actual_length = user_data->actual_length;
	if (user_data->status == LIBUSB_TRANSFER_COMPLETED)
		retval = 0;
	else if (user_data->status == LIBUSB_TRANSFER_TIMED_OUT)
		retval = LIBUSB_ERROR_TIMEOUT;
		
	libusb_free_transfer(xfer);

	pthread_cond_destroy(&user_data->cond);
	pthread_mutex_destroy(&user_data->lock);
	free(user_data);

	return retval;
}


LIBUSB_CALL
libusb_device_handle *libusb_open_device_with_vid_pid(libusb_context *ctx,
                                    uint16_t vendor_id, uint16_t product_id)
{
	struct libusb_device_handle* dev;

	if ((vendor_id != USB_ACTIVETWO_VENDOR_ID)
	  ||(product_id !=  USB_ACTIVETWO_PRODUCT_ID))
		return NULL;

	dev = calloc(1, sizeof(*dev));
	init_device(dev, ctx);
	return dev;
}


LIBUSB_CALL
void libusb_close(libusb_device_handle *dev)
{
	destroy_device(dev);
	free(dev);	
}


LIBUSB_CALL
int libusb_set_configuration(libusb_device_handle *dev, int configuration)
{
	(void)dev;
	(void)configuration;
	return 0;
}


LIBUSB_CALL
int libusb_claim_interface(libusb_device_handle *dev, int iface)
{
	(void)dev;
	(void)iface;
	return 0;
}


LIBUSB_CALL
int libusb_release_interface(libusb_device_handle *dev, int iface)
{
	(void)dev;
	(void)iface;
	return 0;
}


#if NO_LIBUSB_INLINE_HELPER
LIBUSB_CALL
void libusb_fill_bulk_transfer(struct libusb_transfer *transfer,
                               libusb_device_handle *devh,
			       uint8_t endpoint, uint8_t *buf, int length,
			       libusb_transfer_cb_fn callback,
			       void *user_data, uint32_t timeout)
{
	transfer->dev_handle = devh;
	transfer->endpoint = endpoint;
	transfer->type = LIBUSB_TRANSFER_TYPE_BULK;
	transfer->timeout = timeout;
	transfer->buffer = buf;
	transfer->length = length;
	transfer->user_data = user_data;
	transfer->callback = callback;
}
#endif //NO_LIBUSB_INLINE_HELPER

