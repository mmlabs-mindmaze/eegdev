/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef EEGDEV_COMMON_H
#define EEGDEV_COMMON_H

/*
 * This file declares common structures that are used in the implementation
 * But that are not exported
 */

 
#include <pthread.h>
#include <stdbool.h>

#include "eegdev.h"
#include "eegdev-types.h"

#define EGD_ORDER_NONE	0
#define EGD_ORDER_START	1
#define EGD_ORDER_STOP	2

struct selected_channels {
	unsigned int in_offset;
	unsigned int inlen;
	unsigned int buff_offset;
	int in_tsize;
	int buff_tsize;
	union gval sc;
	cast_function cast_fn;
};

struct array_config {
	unsigned int iarray;
	unsigned int arr_offset;
	unsigned int buff_offset;
	unsigned int len;
};


struct egd_chinfo {
	const char *label, *unit, *transducter;
	bool isint;
	int dtype;
	union gval min, max;
};


struct systemcap {
	unsigned int sampling_freq;
	unsigned int type_nch[EGD_NUM_STYPE];
	const char* device_type;
	const char* device_id;
};


// The structure containing the pointer to the methods of the EEG devices
struct eegdev_operations {
/* \param dev	pointer to the eegdev struct of the device
 *
 * Should close the device and free all associated resources.
 * egd_destroy_eegdev should be called in that method
 *
 * Should returns 0 in case of success or -1 if an error occurred (errno
 * should then be set accordingly) */
	int (*close_device)(struct eegdev* dev);


/* \param dev	pointer to the eegdev struct of the device
 * \param ngrp	number of group supplied in the grp array
 * \param grp	pointer to an array of grpconf
 *
 * Called soon after the user has called egd_set_groups. The device
 * implementation can assume that this function will never be called during
 * acquisition (i.e. not between egd_start() and egd_stop())
 *
 * IMPORTANT: in case of success, the device implementation should set
 * dev->in_samlen before returning (unless this has been set once for all
 * at the creation of dev) as well as dev->selch corresponding to the
 * settings describing the transfer between the incoming data from the EEG
 * system to the ringbuffer. The device implementation should assume that
 * dev->selch will allocated before the method is called.
 *
 * Should returns 0 in case of success or -1 if an error occurred (errno
 * should then be set accordingly) */
	int (*set_channel_groups)(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);


/* \param dev	pointer to the eegdev struct of the device
 *
 * Called when the acquisition is about to start.
 *
 * Should returns 0 in case of success or -1 if an error occurred (errno
 * should then be set accordingly) */
	int (*start_acq)(struct eegdev* dev);


/* \param dev	pointer to the eegdev struct of the device
 *
 * Called when the acquisition is about to stop.
 *
 * Should returns 0 in case of success or -1 if an error occurred (errno
 * should then be set accordingly) */
	int (*stop_acq)(struct eegdev* dev);

/* \param dev	pointer to the eegdev struct of the device
 * \param stype	index to the sensor type
 * \param ich	index of the desired channel of the sensor type
 * \param info	pointer to a egd_chinfo structure that must be filled
 *
 * Called when the system need to know information about a particular
 * channel.
 */
	void (*fill_chinfo)(const struct eegdev* dev, int stype,
	                    unsigned int ich, struct egd_chinfo* info);
};


/* \param dev		pointer to the eegdev struct of the device
 * \param in		pointer to an array of samples
 * \param length	size in bytes of the array
 *
 * egd_update_ringbuffer() should be called by the device implementation
 * whenever a new piece of data is available. This function updates the
 * ringbuffer with the data pointed by the pointer in. The array can be
 * incomplete, i.e. it can start and end at a position not corresponding to
 * a boundary of a samples. */
LOCAL_FN
int egd_update_ringbuffer(struct eegdev* dev, const void* in, size_t len);


/* \param dev		pointer to the eegdev struct of the device
 * \param ops		pointer to an structure holding the methods
 *
 * Initialize the eegdev structure pointed by dev and set the methods of
 * the device according to ops. 
 * 
 * IMPORTANT: This function SHOULD be called by the device implementation
 * when it is creating the device structure.
 *
 * Returns 0 in case of success or -1 if an error occurred (errno is then
 * set accordingly) */
LOCAL_FN
int egd_init_eegdev(struct eegdev* dev,const struct eegdev_operations* ops);


/* \param dev		pointer to the eegdev struct of the device
 *
 * Free all resources associated with the eegdev structure pointed by dev.
 * 
 * IMPORTANT: This function SHOULD be called by the device implementation
 * when it is about to close the device.
 *
 * This function is the destructive counterpart of egd_init_eegdev*/
LOCAL_FN
void egd_destroy_eegdev(struct eegdev* dev);


LOCAL_FN
void egd_report_error(struct eegdev* dev, int error);


/* \param dev		pointer to the eegdev struct of the device
 *
 * Initialize the eegdev structure pointed by dev with the information
 * contained in dev->cap
 * 
 * IMPORTANT: This function SHOULD be called by the device implementation
 * when it is opening the device and after egd_init_eegdev has been called.
 */
LOCAL_FN
void egd_update_capabilities(struct eegdev* dev);



struct eegdev {
	const struct eegdev_operations ops;
	struct systemcap cap;
	int provided_stypes[EGD_NUM_STYPE+1];
	unsigned int num_stypes;

	char* buffer;
	size_t buffsize, in_samlen, buff_samlen, in_offset, buff_ns;
	unsigned int ind, last_read, nreadwait;
	unsigned long ns_written, ns_read;
	pthread_mutex_t synclock;
	pthread_cond_t available;
	int acq_order, acquiring;
	int error;

	unsigned int narr;
	size_t *strides;

	unsigned int nsel, nconf;
	struct selected_channels* selch;
	struct array_config* arrconf;
};

struct opendev_options {
	int numch;
	const char* path;
};

#endif //EEGDEV_COMMON_H
