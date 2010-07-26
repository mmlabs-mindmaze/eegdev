#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include "eegdev-common.h"

static char eegdev_string[] = PACKAGE_STRING;
#define BUFF_SIZE	10	//in seconds

/*******************************************************************
 *                Implementation of internals                      *
 *******************************************************************/
static int reterrno(int err)
{
	errno = err;
	return -1;
}

static void optimize_selch(struct selected_channels* selch, unsigned int* ngrp) {
	unsigned int i, j, num = *ngrp;

	for (i=0; i<num; i++) {
		for (j=i+1; j<num; j++) {
			if ( (selch[j].in_offset == selch[i].in_offset+selch[i].len)
			   && (selch[j].buff_offset == selch[i].buff_offset+selch[i].len)
			   && (selch[j].sc.dval == selch[i].sc.dval)
			   && (selch[j].cast_fn == selch[i].cast_fn) ) {
				selch[i].len += selch[j].len;
				memmove(selch + j, selch + j+1, (num-j-1)*sizeof(*selch));
				num--;
				j--;
			}
		}
	}
	*ngrp = num;
}

static int assign_groups(struct eegdev* dev, unsigned int ngrp,
                                        const struct grpconf* grp)
{
	unsigned int i, offset = 0;
		
	for (i=0; i<ngrp; i++) {
		// Set parameters of (ringbuffer -> arrays)
		dev->arrconf[i].len = dev->selch[i].len;
		dev->arrconf[i].iarray = grp[i].iarray;
		dev->arrconf[i].arr_offset = grp[i].arr_offset;
		dev->arrconf[i].buff_offset = offset;
		dev->selch[i].buff_offset = offset;
		offset += dev->selch[i].len;
	}
	dev->buff_samlen = offset;

	// Optimization should take place here
	optimize_selch(dev->selch, &(dev->nsel));

	return 0;
}


static unsigned int cast_data(struct eegdev* restrict dev, const void* restrict in, size_t length)
{
	unsigned int i, ns = 0;
	const char* pi = in;
	char* restrict ringbuffer = dev->buffer;
	const struct selected_channels* sel = dev->selch;
	size_t offset = dev->in_offset, ind = dev->ind;
	ssize_t len, inoff, buffoff, rest, inlen = length;

	while (inlen) {
		for (i=0; i<dev->nsel; i++) {
			len = sel[i].len;
			inoff = sel[i].in_offset - offset;
			buffoff = sel[i].buff_offset;
			if (inoff < 0) {
				len += inoff;
				if (len <= 0)
					continue;
				buffoff -= inoff;
				inoff = 0;
			}
			if ((rest = inlen-inoff) <= 0)
				continue;
			len = (len <= rest) ?  len : rest;
			sel[i].cast_fn(ringbuffer + ind + buffoff, 
			               pi + inoff, sel[i].sc, len);
		}
		rest = dev->in_samlen - offset;
		if (inlen < rest) {
			break;
		}

		inlen -= rest;
		pi += rest;
		offset = 0;
		ns++;
		ind = (ind + dev->buff_samlen) % dev->buffsize;
	}

	pthread_mutex_lock(&dev->synclock);
	dev->ind = ind;
	pthread_mutex_unlock(&dev->synclock);

	return ns;
}


static int validate_groups_settings(struct eegdev* dev, unsigned int ngrp,
                                    const struct grpconf* grp)
{
	unsigned int i, stype;
	unsigned int nmax[EGD_NUM_STYPE] = {
		[EGD_EEG] = dev->cap.eeg_nmax,
		[EGD_TRIGGER] = dev->cap.trigger_nmax,
		[EGD_SENSOR] = dev->cap.sensor_nmax,
	};
	
	// Groups validation
	for (i=0; i<ngrp; i++) {
		stype = grp[i].sensortype;
		if ((stype >= EGD_NUM_STYPE)
		    || (grp[i].index+grp[i].nch > nmax[stype])
		    || (grp[i].datatype >= EGD_NUM_DTYPE)) 
			return reterrno(EINVAL);
	}
	
	return 0;
}


/*******************************************************************
 *                        Systems common                           *
 *******************************************************************/
int egd_init_eegdev(struct eegdev* dev, const struct eegdev_operations* ops)
{	
	int ret;

	memset(dev, 0, sizeof(*dev));
	memcpy((void*)&(dev->ops), ops, sizeof(*ops));

	ret = pthread_cond_init(&(dev->available), NULL);
	if (ret)
		return reterrno(ret);

	ret = pthread_mutex_init(&(dev->synclock), NULL);
	if (ret) {
		pthread_cond_destroy(&(dev->available));
		return reterrno(ret);
	}

	return 0;
}


void egd_destroy_eegdev(struct eegdev* dev)
{	
	pthread_cond_destroy(&(dev->available));
	pthread_mutex_destroy(&(dev->synclock));
	
	free(dev->selch);
	free(dev->arrconf);
	free(dev->strides);
	free(dev->buffer);
}


// TODO: Detect ringbuffer full
int egd_update_ringbuffer(struct eegdev* dev, const void* in, size_t length)
{
	unsigned int ns, rest;
	pthread_mutex_t* synclock = &(dev->synclock);

	// Process acquisition order
	pthread_mutex_lock(synclock);
	if (dev->acq_order == EGD_ORDER_START) {
		// Check if we can start the acquisition now. If not
		// postpone it to a later call of update_ringbuffer, i.e. do
		// not reset the order
		rest = (dev->in_samlen - dev->in_offset) % dev->in_samlen;
		if (rest <= length) {
			dev->acq_order = EGD_ORDER_NONE;

			// realign on beginning of the next sample
			// (avoid junk at the beginning of the acquisition)
			in = (char*)in + rest;
			length -= rest;
			dev->in_offset = 0;
		}
	} else if (dev->acq_order == EGD_ORDER_STOP) {
		dev->acq_order = EGD_ORDER_NONE;
		dev->acquiring = 0;
	}
	pthread_mutex_unlock(synclock);


	if (dev->acquiring) {
		// Put data on the ringbuffer
		ns = cast_data(dev, in, length);

		// Update number of sample available and signal if
		// thread is waiting for data
		pthread_mutex_lock(synclock);
		dev->ns_written += ns;
		if (dev->waiting
		   && (dev->nreading + dev->ns_read <= dev->ns_written))
			pthread_cond_signal(&(dev->available));
		pthread_mutex_unlock(synclock);
	}

	dev->in_offset = (length + dev->in_offset) % dev->in_samlen;
	return 0;
}


/*******************************************************************
 *                    API functions implementation                 *
 *******************************************************************/
int egd_get_cap(const struct eegdev* dev, struct systemcap *capabilities)
{
	if (!dev || !capabilities)
		return reterrno(EINVAL);

	memcpy(capabilities, &(dev->cap), sizeof(*capabilities));
	return 0;
}


int egd_close(struct eegdev* dev)
{
	int acquiring;
	if (!dev)
		return reterrno(EINVAL);

	pthread_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	pthread_mutex_unlock(&(dev->synclock));
	if (acquiring)
		egd_stop(dev);

	dev->ops.close_device(dev);
	return 0;
}


int egd_acq_setup(struct eegdev* dev, 
                  unsigned int narr, const size_t *strides,
		  unsigned int ngrp, const struct grpconf *grp)
{
	int acquiring;

	if (!dev || (ngrp && !grp) || (narr && !strides)) 
		return reterrno(EINVAL);
	
	pthread_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	pthread_mutex_unlock(&(dev->synclock));
	if (acquiring)
		return reterrno(EPERM);

	if (validate_groups_settings(dev, ngrp, grp))
		return -1;
	
	// Alloc transfer configuration structs
	free(dev->selch);
	free(dev->arrconf);
	dev->strides = malloc(narr*sizeof(*strides));
	dev->selch = calloc(ngrp,sizeof(*(dev->selch)));
	dev->arrconf = calloc(ngrp,sizeof(*(dev->arrconf)));
	if (!dev->selch || !dev->arrconf || !dev->strides)
		return -1;
	dev->nsel = dev->nconf = ngrp;

	// Update arrays details
	dev->narr = narr;
	memcpy(dev->strides, strides, narr*sizeof(*strides));

	// Setup transfer configuration (this call affects ringbuffer size)
	if (dev->ops.set_channel_groups(dev, ngrp, grp))
		return -1;
	assign_groups(dev, ngrp, grp);

	// Alloc ringbuffer
	free(dev->buffer);
	dev->buffsize = BUFF_SIZE*dev->cap.sampling_freq * dev->buff_samlen;
	dev->buffer = malloc(dev->buffsize);
	if (!dev->buffer)
		return -1;

	return 0;
}


ssize_t egd_get_data(struct eegdev* dev, size_t ns, ...)
{
	if (!dev)
		return reterrno(EINVAL);

	unsigned int i, s, iarr, curr_s = dev->last_read;
	struct array_config* ac = dev->arrconf;
	char* restrict ringbuffer = dev->buffer;
	char* restrict buffout[dev->narr];
	va_list ap;

	va_start(ap, ns);
	for (i=0; i<dev->narr; i++) 
		buffout[i] = va_arg(ap, char*);
	va_end(ap);

	// Wait until there is enough data in ringbuffer or the acquisition
	// stops. If the acquisition is stopped, the number of sample read
	// MAY be smaller than requested
	pthread_mutex_lock(&(dev->synclock));
	dev->nreading = ns;
	dev->waiting = 1;
	while ((dev->ns_read+ns > dev->ns_written) && dev->acquiring)
		pthread_cond_wait(&(dev->available), &(dev->synclock));
	dev->waiting = 0;
	if (dev->acquiring && (dev->ns_written - dev->ns_read < ns))
		dev->nreading = ns = dev->ns_written - dev->ns_read;
	pthread_mutex_unlock(&(dev->synclock));

	// Copy data from ringbuffer to arrays
	for (s=0; s<ns; s++) {
		for (i=0; i<dev->nconf; i++) {
			iarr = ac[i].iarray;
			memcpy(buffout[iarr] + ac[i].arr_offset,
			       ringbuffer + curr_s + ac[i].buff_offset,
			       ac[i].len);
		}

		curr_s = (curr_s + dev->buff_samlen) % dev->buffsize;
		for (i=0; i<dev->narr; i++)
			buffout[i] += dev->strides[i];
	}

	// Update the reading status
	pthread_mutex_lock(&(dev->synclock));
	dev->ns_read += ns;
	dev->nreading = 0;
	dev->last_read = curr_s;
	pthread_mutex_unlock(&(dev->synclock));

	return ns;
}


ssize_t egd_get_available(struct eegdev* dev)
{
	int ns;

	if (!dev)
		return reterrno(EINVAL);

	pthread_mutex_lock(&(dev->synclock));
	ns = dev->ns_written - dev->ns_read;
	pthread_mutex_unlock(&(dev->synclock));

	return ns;
}


int egd_start(struct eegdev* dev)
{
	int acquiring;

	if (!dev)
		return reterrno(EINVAL);

	pthread_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	pthread_mutex_unlock(&(dev->synclock));
	if (acquiring)
		return reterrno(EPERM);
	
	dev->ns_read = dev->ns_written = 0;
	dev->ops.start_acq(dev);

	pthread_mutex_lock(&(dev->synclock));
	dev->acq_order = EGD_ORDER_START;
	dev->acquiring = 1;
	pthread_mutex_unlock(&(dev->synclock));

	return 0;
}


int egd_stop(struct eegdev* dev)
{
	int acquiring;

	if (!dev)
		return reterrno(EINVAL);

	pthread_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	pthread_mutex_unlock(&(dev->synclock));
	if (!acquiring)
		return reterrno(EPERM);

	pthread_mutex_lock(&(dev->synclock));
	dev->acq_order = EGD_ORDER_STOP;
	pthread_mutex_unlock(&(dev->synclock));

	dev->ops.stop_acq(dev);
	return 0;
}

const char* egd_get_string(void)
{
	return eegdev_string;	
}
