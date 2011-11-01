/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef EEGDEV_COMMON_H
#define EEGDEV_COMMON_H

 
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "eegdev.h"

#ifdef __cplusplus
#define EGDI_CALL	extern "C"
#else
#define EGDI_CALL
#endif

#define EEGDEV_PLUGIN_ABI_VERSION	1

union gval {
	float valfloat;
	double valdouble;
	int32_t valint32_t;
};

typedef void (*cast_function)(void* restrict, const void* restrict,
                              union gval, size_t);


#define EGD_ORDER_NONE	0
#define EGD_ORDER_START	1
#define EGD_ORDER_STOP	2

struct selected_channels {
	// To be set by device implementation
	union gval sc; // to be set if bsc != 0
	unsigned int in_offset;
	unsigned int inlen;
	unsigned int typein, typeout;
	unsigned int iarray;
	unsigned int arr_offset;
	int bsc;
	int padding;
};

struct input_buffer_group {
	// Computed values
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

#define EGD_LABEL_LEN		32
#define EGD_UNIT_LEN		16
#define EGD_TRANSDUCTER_LEN	128
#define EGD_PREFILTERING_LEN	128

struct egd_chinfo {
	const char *label, *unit, *transducter, *prefiltering;
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
 * Should returns 0 in case of success or -1 if an error occurred (errno
 * should then be set accordingly).
 *
 * IMPORTANT: in case of success, before returning, the device
 * implementation should have informed the core library how it will supply
 * data to the ringbuffer. This means, it has:
 *    - allocated the the necessary input groups by calling the core library
 *      function dev->ci.alloc_input_groups()
 *    - configured the returned array of struct selected_channels
 *    - call dev->ci.set_input_samlen()
 * If applicable, the first two point can be done almost completely by a
 * call egdi_split_alloc_chgroups in device-helper.h */
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
 * channel. */
	void (*fill_chinfo)(const struct eegdev* dev, int stype,
	                    unsigned int ich, struct egd_chinfo* info);
};


struct core_interface {
/* \param dev		pointer to the eegdev struct of the device
 * \param in		pointer to an array of samples
 * \param length	size in bytes of the array
 *
 * egd_update_ringbuffer() should be called by the device implementation
 * whenever a new piece of data is available. This function updates the
 * ringbuffer with the data pointed by the pointer in. The array can be
 * incomplete, i.e. it can start and end at a position not corresponding to
 * a boundary of a samples. */
	EGDI_CALL int (*update_ringbuffer)(struct eegdev* dev,
	                                   const void* in, size_t len);


/* \param dev		pointer to the eegdev struct of the device
 * \param num_ingrp	number of channels group sent to the ringbuffer
 *
 * Specifies the number of channels groups the device implementation is
 * going to send to the ringbuffer. 
 *
 * This function returns the allocated in case of success, NULL otherwise.
 *
 * IMPORTANT: This function SHOULD be called by the device implementation
 * while executing set_channel_groups mthod.
 */
	EGDI_CALL struct selected_channels* (*alloc_input_groups)(
	                                         struct eegdev* dev,
                                                unsigned int num_ingrp);

	EGDI_CALL void (*report_error)(struct eegdev* dev, int error);

	EGDI_CALL const char* (*getopt)(const char* option,
	                                const char* defaultval, 
                                        const char* optv[]);


/* \param dev		pointer to the eegdev struct of the device
 * \param samlen	size in bytes of one sample
 *
 * Specifies the size of one sample as it is supplied to the function
 * egd_update_ringbuffer. 
 *
 * IMPORTANT: This function SHOULD be called by the device implementation
 * before the first call to egd_update_ringbuffer and before the method
 * set_channel_groups returns. */
	EGDI_CALL void (*set_input_samlen)(struct eegdev* dev,
	                                   unsigned int samlen);
};

struct egdi_plugin_info {
	unsigned int plugin_abi;
	unsigned int struct_size;
	int (*open_device)(struct eegdev*, const char*[]);
	int (*close_device)(struct eegdev*);
	int (*set_channel_groups)(struct eegdev*, unsigned int,
	                                            const struct grpconf*);
	int (*start_acq)(struct eegdev* dev);
	int (*stop_acq)(struct eegdev* dev);
	void (*fill_chinfo)(const struct eegdev*, int,
	                                 unsigned int, struct egd_chinfo*);
};


struct eegdev {
	const struct eegdev_operations ops;
	const struct core_interface ci;
	struct systemcap cap;
	int provided_stypes[EGD_NUM_STYPE+1];
	unsigned int num_stypes;

	char* buffer;
	size_t buffsize, in_samlen, buff_samlen, in_offset, buff_ns;
	unsigned int ind, last_read, nreadwait;
	unsigned long ns_written, ns_read;
	pthread_mutex_t synclock;
	pthread_mutex_t apilock;
	pthread_cond_t available;
	int acq_order, acquiring;
	int error;

	unsigned int narr;
	size_t *strides;

	unsigned int ngrp, nsel, nconf;
	struct input_buffer_group* inbuffgrp;
	struct selected_channels* selch;
	struct array_config* arrconf;

	void* handle;
};


static inline
unsigned int egd_get_data_size(unsigned int type)
{
	unsigned int size = 0;

	if (type == EGD_INT32)		
		size = sizeof(int32_t);
	else if (type == EGD_FLOAT)
		size = sizeof(float);
	else if (type == EGD_DOUBLE)
		size = sizeof(double);
	
	return size;
}



#endif //EEGDEV_COMMON_H
