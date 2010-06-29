#ifndef EEGDEV_COMMON_H
#define EEGDEV_COMMON_H

#include <pthread.h>

#include "eegdev.h"
#include "eegdev-types.h"

struct eegdev_operations {
	int (*close_device)(struct eegdev* dev);
	int (*start_acq)(struct eegdev* dev);
	int (*stop_acq)(struct eegdev* dev);
	int (*set_channel_groups)(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);
};


struct selected_channels {
	unsigned int in_offset;
	unsigned int len;
	unsigned int buff_offset;
	union scale sc;
	cast_function cast_fn;
};

struct array_config {
	unsigned int iarray;
	unsigned int arr_offset;
	unsigned int buff_offset;
	unsigned int len;
};

struct eegdev {
	const struct eegdev_operations ops;
	struct systemcap cap;

	char* buffer;
	size_t buffsize, in_samlen, buff_samlen, in_offset;
	unsigned int ind, last_read, req_to_read;
	unsigned long ns_written, ns_read;
	pthread_mutex_t synclock;
	pthread_cond_t available;
	int acq;

	unsigned int narr;
	size_t *strides;

	unsigned int nsel, nconf;
	struct selected_channels* selch;
	struct array_config* arrconf;

	unsigned int sampling_freq;
};

void update_ringbuffer(struct eegdev* dev, const void* in, size_t length);
int init_eegdev(struct eegdev* dev, const struct eegdev_operations* ops);

#endif //EEGDEV_COMMON_H