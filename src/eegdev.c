#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "eegdev-common.h"

static int reterrno(int err)
{
	errno = err;
	return -1;
}


void cast_data(struct eegdev* dev, const void* in, size_t length)
{
	unsigned int i;
	const char* pi = in;
	const struct selected_channels* sel = dev->selch;
	size_t offset = dev->in_samind;
	ssize_t len, inoff, buffoff, rest, inlen = length;

	while (inlen) {
		for (i=0; i<dev->ngrp; i++) {
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
			sel[i].cast_fn(dev->buffer + dev->ind + buffoff, 
			               pi + inoff, sel[i].sc, len);
		}
		rest = dev->in_samlen - offset;
		if (inlen < rest) {
			break;
		}
		inlen -= rest;
		pi += rest;
		offset = 0;
		dev->ind += dev->buff_samlen;
		if (dev->ind >= dev->buffsize)
			dev->ind = 0;
	}

	dev->in_samind = inlen + offset;
}


static int update_data_fn(struct eegdev* dev)
{
	ssize_t len;
	const void *buffin; 

	while (1) {
		buffin = dev->ops.update_data(dev, &len);
		cast_data(dev, buffin, len);
	}

	return 0;
}

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

	
	dev->ops.set_channel_groups(dev, ngrp, grp);
	return 0;
}
