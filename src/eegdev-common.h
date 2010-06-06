#include "eegdev.h"


struct eegdev_operations {
	void (*free_device)(struct eegdev* dev);
	void (*set_channel_groups)(struct eegdev* dev, unsigned int ngrp,
						struct grpconf* grp);
};

struct eegdev {
	struct eegdev_operations ops;
	struct eegcap cap;
	void* buffer;
	size_t buffsize, samplesize, pointer;
};

void cast_data(const struct eegdev* dev, unsigned int ns, const void* in);

