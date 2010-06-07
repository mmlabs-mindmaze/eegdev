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


int egd_get_cap(const struct eegdev* dev, struct systemcap *capabilities)
{
	if (!dev || !cap)
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
}
