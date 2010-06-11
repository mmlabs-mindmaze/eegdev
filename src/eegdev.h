/*
 * 	Proposal for an common EEG device API
 */

#ifndef EEGDEV_H
#define EEGDEV_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cpluplus
extern "C" {
#endif // __cplusplus

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
#define EGD_NUM_SENSOR	3

struct eegdev;

struct grpconf {
	unsigned int sensortype;
	unsigned int ch_offset;
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
int egd_decl_arrays(struct eegdev* dev, unsigned int narr, 
					const size_t *strides);
int egd_set_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);
int egd_start(struct eegdev* dev);
int egd_get_data(struct eegdev* dev, unsigned int ns, ...);
int egd_stop(struct eegdev* dev);
int egd_get_quality(struct eegdev* dev /* TO BE DETERMINED */);


struct eegdev* egd_open_biosemi(void);

#ifdef __cpluplus
}
#endif // __cplusplus

#endif //EEGDEV_H
