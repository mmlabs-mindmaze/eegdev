/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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

#include <pthread.h>
#include <libusb.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "usb_comm.h"

static unsigned int nctxref = 0;
static int close_ctx = 0;
static libusb_context* local_ctx;
static pthread_t evt_thread;

static pthread_mutex_t ctxmtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t closemtx = PTHREAD_MUTEX_INITIALIZER;

static void* usb_event_handling_proc(void* arg)
{
	(void)arg;
	int must_close;
	struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};

	while (1) {
		pthread_mutex_lock(&(closemtx));
		must_close = close_ctx;
		pthread_mutex_unlock(&(closemtx));
		if (must_close)
			break;

		libusb_handle_events_timeout(local_ctx, &tv);
	}

	return NULL;
}

LOCAL_FN
libusb_context* egd_acquire_usb_context(void)
{
	pthread_mutex_lock(&(ctxmtx));
	if (nctxref++ == 0) {
		libusb_init(&local_ctx);
		pthread_create(&evt_thread, NULL, 
			       usb_event_handling_proc, NULL);
	}
	pthread_mutex_unlock(&(ctxmtx));

	return local_ctx;
}


LOCAL_FN
void egd_release_usb_context(void)
{
	pthread_mutex_lock(&(ctxmtx));
	if (--nctxref == 0) {
		pthread_mutex_lock(&(closemtx));
		close_ctx = 1;
		pthread_mutex_unlock(&(closemtx));
		
		pthread_join(evt_thread, NULL);
		libusb_exit(local_ctx);
		close_ctx = 0;
	}
	pthread_mutex_unlock(&(ctxmtx));
}


/**************************************************************************
 *                                                                        *
 *                    Buffered USB transfer                               *
 *                                                                        *
 **************************************************************************/
static int translate_transfer_err(enum libusb_transfer_status status)
{
	int error;

	if (status == LIBUSB_TRANSFER_COMPLETED)
		error = 0;
	else if(status == LIBUSB_TRANSFER_CANCELLED)
		error = -1;
	else if (status == LIBUSB_TRANSFER_TIMED_OUT) 
		error = EAGAIN;
	else if (status == LIBUSB_TRANSFER_NO_DEVICE)
		error = ENODEV;
	else
		error = EIO;
	
	return error;
}


static int submit_read_urb(struct usb_btransfer* btr)
{
	int ret;
	
	btr->urb->buffer = btr->chunkbuff[btr->iurb];
	ret = libusb_submit_transfer(btr->urb);

	if (ret == LIBUSB_ERROR_NO_DEVICE)
		errno = ENODEV;
	else if (ret)
		errno = EIO;
	
	return (ret == 0) ? 0 : -1;
}

#ifndef LIBUSB_CALL
#define LIBUSB_CALL
#endif
static void LIBUSB_CALL req_completion_fn(struct libusb_transfer *transfer)
{
	struct usb_btransfer* ubtr = transfer->user_data;
	int error = 0;

	ubtr->actual_length[ubtr->iurb] = transfer->actual_length;
	error = translate_transfer_err(transfer->status);
	if (transfer->status == LIBUSB_TRANSFER_STALL)
		libusb_clear_halt(ubtr->urb->dev_handle, ubtr->urb->endpoint);

	pthread_mutex_lock(&(ubtr->mtx));
	if (ubtr->stop || error)
		ubtr->state = error ? error : -1;
	
	// resume execution of waiting swap or waiting stop
	if ((ubtr->state) || (ubtr->iurb == ubtr->iread))
		pthread_cond_signal(&(ubtr->cond));
	
	// Advance to the next buffer
	ubtr->iurb++;
	ubtr->iurb %= NCHUNKBUFF;

	// Resubmit the urb if there is no error (or cancellation) and
	// if the main thread (egd_swap_usb_transfer) is not slowing down
	if (!ubtr->state && (ubtr->iurb != ubtr->iread)) 
		if (submit_read_urb(ubtr))
			ubtr->state = errno;
	pthread_mutex_unlock(&(ubtr->mtx));
}


LOCAL_FN
int egd_swap_usb_btransfer(struct usb_btransfer* ubtr, void** buff)
{
	int ns, error = 0;

	pthread_mutex_lock(&(ubtr->mtx));
	// egd_getdata_usb_btransfer is waited so it must resubmit the urb
	if (!ubtr->state && !ubtr->stop && (ubtr->iurb == ubtr->iread))
		if (submit_read_urb(ubtr))
			ubtr->state = errno;

	ubtr->iread++;
	ubtr->iread %= NCHUNKBUFF;
	if (!ubtr->state && (ubtr->iurb == ubtr->iread))
		pthread_cond_wait(&(ubtr->cond), &(ubtr->mtx));
	
	error = ubtr->state; // error might have occured during cond_wait
	pthread_mutex_unlock(&(ubtr->mtx));

	if (error) {
		errno = (error < 0) ? EIO : error;
		return -1;
	}
	
	*buff = ubtr->chunkbuff[ubtr->iread];
	ns = ubtr->actual_length[ubtr->iread];

	return ns;
}


LOCAL_FN
int egd_start_usb_btransfer(struct usb_btransfer* ubtr)
{
	ubtr->iurb = 0;
	ubtr->iread = -1;
	ubtr->state = 0;
	ubtr->stop = 0;
	return submit_read_urb(ubtr);
}


LOCAL_FN
void egd_stop_usb_btransfer(struct usb_btransfer* ubtr)
{
	pthread_mutex_lock(&(ubtr->mtx));
	ubtr->stop = 1;
	if (libusb_cancel_transfer(ubtr->urb) == 0)
		// Wait for the cancellation to complete
		while (ubtr->state)
			pthread_cond_wait(&(ubtr->cond), &(ubtr->mtx));
	pthread_mutex_unlock(&(ubtr->mtx));
}

static void* page_aligned_malloc(size_t len)
{
#if HAVE_POSIX_MEMALIGN && HAVE_SYSCONF
	void* memptr;
	size_t pgsize = sysconf(_SC_PAGESIZE);
	if (posix_memalign(&memptr, pgsize, len)) {
		errno = ENOMEM;
		return NULL;
	}
	else
		return memptr;
#else
	return malloc(len);
#endif
}

LOCAL_FN
int egd_init_usb_btransfer(struct usb_btransfer* ubtr, 
			   libusb_device_handle* hudev, int ep,
			   size_t len, unsigned int to_ms)
{
	int i, retm = -1, retc = -1;

	memset(ubtr, 0, sizeof(*ubtr));
	for (i=0; i<NCHUNKBUFF; i++)
		ubtr->chunkbuff[i] = page_aligned_malloc(len);

	ubtr->urb = libusb_alloc_transfer(0);

	if ( (retc = pthread_cond_init(&(ubtr->cond), NULL)) 
	  || (retm = pthread_mutex_init(&(ubtr->mtx), NULL)) )
		goto error;

	libusb_fill_bulk_transfer(ubtr->urb, hudev, ep, NULL, len,
	                          req_completion_fn, ubtr, to_ms);

	return 0;

error:
	if (retc == 0)
		pthread_cond_destroy(&(ubtr->cond));
	if (retm == 0)
		pthread_mutex_destroy(&(ubtr->mtx));
	if (retc > 0 || retm > 0)
		errno = (retm > 0) ? retm : retc;

	libusb_free_transfer(ubtr->urb);
	for (i=0; i<NCHUNKBUFF; i++)
		free(ubtr->chunkbuff[i]);

	return -1;
}


LOCAL_FN
void egd_destroy_usb_btransfer(struct usb_btransfer* ubtr)
{
	int i;

	pthread_cond_destroy(&(ubtr->cond));
	pthread_mutex_destroy(&(ubtr->mtx));
	libusb_free_transfer(ubtr->urb);
	for (i=0; i<NCHUNKBUFF; i++)
		free(ubtr->chunkbuff[i]);
}
