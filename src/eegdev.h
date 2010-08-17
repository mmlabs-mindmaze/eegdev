#ifndef EEGDEV_H
#define EEGDEV_H

#include <sys/types.h>

#ifdef __cpluplus
extern "C" {
#endif 

/*************************************************************************
 *                          API definitions                              *
 ************************************************************************/
// Supported data types
#define EGD_INT32	0
#define EGD_FLOAT	1
#define EGD_DOUBLE	2
#define EGD_NUM_DTYPE	3

// Supported sensor types
#define EGD_EEG		0
#define EGD_TRIGGER	1
#define EGD_SENSOR	2
#define EGD_NUM_STYPE	3

struct eegdev;

struct grpconf {
	unsigned int sensortype;
	unsigned int index;
	unsigned int nch;
	unsigned int iarray;
	unsigned int arr_offset;
	unsigned int datatype;
};

struct systemcap {
	unsigned int sampling_freq;
	unsigned int eeg_nmax;
	unsigned int sensor_nmax;
	unsigned int trigger_nmax;
};


int egd_get_cap(const struct eegdev* dev, struct systemcap *capabilities);
int egd_close(struct eegdev* dev);
int egd_acq_setup(struct eegdev* dev,
                  unsigned int narr, const size_t *strides,
                  unsigned int ngrp, const struct grpconf* grp);
int egd_start(struct eegdev* dev);
ssize_t egd_get_data(struct eegdev* dev, size_t ns, ...);
ssize_t egd_get_available(struct eegdev* dev);
int egd_stop(struct eegdev* dev);
int egd_get_quality(struct eegdev* dev /* TO BE DETERMINED */);
const char* egd_get_string(void);


struct eegdev* egd_open_biosemi(void);
struct eegdev* egd_open_file(const char* filename, const unsigned int grpindex[EGD_NUM_STYPE]);

#ifdef __cpluplus
}
#endif

#endif //EEGDEV_H
