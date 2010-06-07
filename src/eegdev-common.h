#include "eegdev.h"
#include <pthread.h>

struct eegdev_operations {
	void (*close_device)(struct eegdev* dev);
	void (*set_channel_groups)(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);
	int (*update_data)(struct eegdev* dev);
};

typedef void (*cast_function)(void* out, const void* in, size_t len);

struct selected_channels {
	unsigned int ch_offset;
	unsigned int nch;
	unsigned int iarray;
	unsigned int arr_offset;
	cast_function cast_fn;
};

struct eegdev {
	const struct eegdev_operations ops;
	struct eegcap cap;

	void* buffer;
	size_t buffsize, samplesize, pointer;
	pthread_mutex_t update_mtx;
	pthread_t thread_id;

	unsigned int narr;
	size_t *strides;

	unsigned int ngrp;
	struct selected_channels selch;
};

void cast_data(const struct eegdev* dev, unsigned int ns, const void* in);

