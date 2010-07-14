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


static unsigned int cast_data(struct eegdev* dev, const void* in, size_t length)
{
	unsigned int i, ns = 0;
	const char* pi = in;
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
				buffoff += inoff;
				inoff = 0;
			}
			if ((rest = inlen-inoff) <= 0)
				continue;
			len = (inoff+len < rest) ?  len : rest;
			sel[i].cast_fn(dev->buffer + ind + buffoff, 
			               pi + inoff, sel[i].sc, len);
		}
		rest = dev->in_samlen - offset;
		if (inlen < rest)
			break;

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
int init_eegdev(struct eegdev* dev, const struct eegdev_operations* ops)
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


// TODO: Detect ringbuffer full
int update_ringbuffer(struct eegdev* dev, const void* in, size_t length)
{
	unsigned int acquire, ns;
	pthread_mutex_t* synclock = &(dev->synclock);

	pthread_mutex_lock(synclock);
	acquire = dev->acq;
	pthread_mutex_unlock(synclock);

	if (acquire) {
		ns = cast_data(dev, in, length);

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
	if (!dev)
		return reterrno(EINVAL);

	if (dev->acq)
		egd_stop(dev);

	pthread_cond_destroy(&(dev->available));
	pthread_mutex_destroy(&(dev->synclock));
	
	free(dev->selch);
	free(dev->arrconf);
	free(dev->strides);
	free(dev->buffer);

	dev->ops.close_device(dev);
	return 0;
}


int egd_decl_arrays(struct eegdev* dev, unsigned int narr, 
					const size_t* strides)
{
	size_t *newstrides;

	if (!dev || (narr && !strides))
		return reterrno(EINVAL);

	// Safe allocation
	if (narr != dev->narr) {
		newstrides = malloc(narr*sizeof(*strides));
		if (!newstrides)
			return -1;

		free(dev->strides);
		dev->strides = newstrides;
	}

	// Update arrays details
	dev->narr = narr;
	memcpy(dev->strides, strides, narr*sizeof(*strides));

	return 0;
}


int egd_set_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	if (!dev || (ngrp && !grp) || dev->acq) 
		return reterrno(dev->acq ? EPERM : EINVAL);

	if (validate_groups_settings(dev, ngrp, grp))
		return -1;
	
	// Alloc transfer configuration structs
	free(dev->selch);
	free(dev->arrconf);
	dev->selch = calloc(ngrp,sizeof(*(dev->selch)));
	dev->arrconf = calloc(ngrp,sizeof(*(dev->arrconf)));
	if (!dev->selch || !dev->arrconf)
		return -1;
	dev->nsel = dev->nconf = ngrp;

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


int egd_get_data(struct eegdev* dev, unsigned int ns, ...)
{
	if (!dev)
		return reterrno(EINVAL);

	unsigned int i, s, iarr, curr_s = dev->last_read;
	struct array_config* ac = dev->arrconf;
	va_list ap;
	char* buffout[dev->narr];

	va_start(ap, ns);
	for (i=0; i<dev->narr; i++) 
		buffout[i] = va_arg(ap, char*);
	va_end(ap);

	// Wait until there is enough data in ringbuffer
	pthread_mutex_lock(&(dev->synclock));
	dev->waiting = 1;
	dev->nreading = ns;
	while (dev->ns_read + ns > dev->ns_written)
		pthread_cond_wait(&(dev->available), &(dev->synclock));
	dev->waiting = 0;
	pthread_mutex_unlock(&(dev->synclock));

	// Copy data from ringbuffer to arrays
	for (s=0; s<ns; s++) {
		for (i=0; i<dev->nconf; i++) {
			iarr = ac[i].iarray;
			memcpy(buffout[iarr] + ac[i].arr_offset,
			       dev->buffer + curr_s + ac[i].buff_offset,
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

	return 0;
}


int egd_get_available(struct eegdev* dev)
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
	if (!dev || dev->acq)
		reterrno(!dev ? EINVAL : EPERM);

	dev->ns_read = dev->ns_written = 0;

	dev->ops.start_acq(dev);

	pthread_mutex_lock(&(dev->synclock));
	dev->acq = 1;
	pthread_mutex_unlock(&(dev->synclock));

	return 0;
}


int egd_stop(struct eegdev* dev)
{
	if (!dev || !(dev->acq))
		reterrno(!dev ? EINVAL : EPERM);

	pthread_mutex_lock(&(dev->synclock));
	dev->acq = 0;
	pthread_mutex_unlock(&(dev->synclock));

	dev->ops.stop_acq(dev);
	return 0;
}

const char* egd_get_string(void)
{
	return eegdev_string;	
}
