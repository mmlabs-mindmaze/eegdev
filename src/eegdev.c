#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include "eegdev-common.h"

static int reterrno(int err)
{
	errno = err;
	return -1;
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


void update_ringbuffer(struct eegdev* dev, const void* in, size_t length)
{
	unsigned int acquire, newoff, ns_added, ns_read, ns_written;

	pthread_mutex_lock(&(dev->synclock));
	acquire = dev->acq;
	pthread_mutex_unlock(&(dev->synclock));
	dev->ns_written = ns_written;

	if (acquire) {
		ns_written += cast_data(dev, in, length);

		pthread_mutex_lock(&(dev->synclock));
		dev->ns_written = ns_written;
		if (dev->req_to_read) 
			if (dev->req_to_read + dev->ns_read > ns_written) 
				pthread_cond_signal(&(dev->available));
		pthread_mutex_unlock(&(dev->synclock));
	}

	dev->in_offset = (length + dev->in_offset) % dev->in_samlen;
}


int egd_get_cap(const struct eegdev* dev, struct systemcap *capabilities)
{
	if (!dev || !capabilities)
		return reterrno(EINVAL);

	memcpy(capabilities, &(dev->cap), sizeof(*capabilities));
	return 0;
}

int init_eegdev(struct eegdev* dev, const struct eegdev_operations* ops)
{	
	memset(dev, 0, sizeof(*dev));
	memcpy((struct eegdev_operations*)&(dev->ops), ops, sizeof(*ops));

	return 0;
}

int egd_close(struct eegdev* dev)
{
	if (!dev)
		return reterrno(EINVAL);

	pthread_cond_destroy(&(dev->available));
	pthread_mutex_destroy(&(dev->synclock));
	
	free(dev->selch);
	free(dev->arrconf);
	free(dev->strides);

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
	if (!dev || (ngrp && !grp)) 
		return reterrno(EINVAL);

	if (dev->acq)
		return reterrno(EPERM);
	
	// Alloc transfer configuration structs
	free(dev->selch);
	free(dev->arrconf);
	dev->selch = malloc(ngrp*sizeof(*(dev->selch)));
	dev->arrconf = malloc(ngrp*sizeof(*(dev->arrconf)));
	if (!dev->selch || !dev->arrconf)
		return -1;
	dev->nsel = dev->nconf = ngrp;

	// Setup transfer configuration (this call affects ringbuffer size)
	if (dev->ops.set_channel_groups(dev, ngrp, grp))
		return -1;

	// Alloc ringbuffer
	free(dev->buffer);
	dev->buffsize = dev->sampling_freq * dev->buff_samlen;
	dev->buffer = malloc(dev->buffsize);
	if (!dev->buffer)
		return -1;
	return 0;
}


int egd_get_data(struct eegdev* dev, unsigned int ns, ...)
{
	unsigned int i, s, iarr, curr_s = dev->last_read;
	struct array_config* ac = dev->arrconf;
	int rc;
	va_list ap;
	char* buffout[dev->narr];

	va_start(ap, ns);
	for (i=0; i<dev->narr; i++)
		buffout[i] = va_arg(ap, char*);
	va_end(ap);

	// Wait until there is enough data
	pthread_mutex_lock(&(dev->synclock));
	rc = 0;
	while ((dev->ns_read + ns > dev->ns_written) && !rc)
		rc = pthread_cond_wait(&(dev->available), &(dev->synclock));
	pthread_mutex_unlock(&(dev->synclock));

	// Copy data from ringbuffer to arrays
	for (s=0; s<ns; s++) {
		for (i=0; i<dev->nconf; i++) {
			iarr = ac[i].iarray;
			memcpy(buffout[i] + ac[i].buff_offset,
			       dev->buffer + ac[i].arr_offset,
			       ac[i].len);
		}

		curr_s = (curr_s + dev->buff_samlen) % dev->buffsize;
		for (i=0; i<dev->narr; i++)
			buffout[i] += dev->strides[i];
	}

	// Update the status
	pthread_mutex_lock(&(dev->synclock));
	dev->ns_read += ns;
	pthread_mutex_unlock(&(dev->synclock));

	return 0;
}

