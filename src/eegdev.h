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

// Supported sensor types
#define EGD_EEG		0
#define EGD_TRIGGER	1
#define EGD_SENSOR	2

struct eegdev*;

struct egdgrpconf {
	unsigned int sensortype;
	unsigned int ch_offset;
	unsigned int nch;
	unsigned int iarray;
	unsigned int arr_offset;
	unsigned int datatype;
};

/**************************************************************************
 *                            API Functions                               * 
 *                                                                        *
 *  IMPORTANT NOTE: There is no open function. This is normal. The open   *
 * function is the only system-specific function seen by the user. Its    *
 * only requirement is returning a eegdev handle. The arguments of the   *
 * function can be anything necessary                                     *
 *************************************************************************/

int egd_get_cap(struct eegdev* dev, int cap);
int egd_close(struct eegdev* dev);
int egd_decl_arrays(struct eegdev* eeg, unsigned int narr, size_t strides);
int egd_set_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);
int egd_start(struct eegdev* dev);
int egd_get_data(struct eegdev* dev, unsigned int ns, ...);
int egd_stop(struct eegdev* dev);
int egd_get_quality(struct eegdev* dev, ...);

#ifdef __cpluplus
}
#endif // __cplusplus

#endif //EEGDEV_H
