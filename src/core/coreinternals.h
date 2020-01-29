/*
    Copyright (C) 2010-2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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
#ifndef COREINTERNALS_H
#define COREINTERNALS_H

#include <stdint.h>
#include <stddef.h>
#include <mmthread.h>
#include "eegdev.h"
#include "eegdev-pluginapi.h"

#define EGD_ORDER_NONE	0
#define EGD_ORDER_START	1
#define EGD_ORDER_STOP	2

#define EGD_LABEL_LEN		32
#define EGD_UNIT_LEN		16
#define EGD_TRANSDUCER_LEN	128
#define EGD_PREFILTERING_LEN	128

struct conf;

LOCAL_FN void egd_destroy_eegdev(struct eegdev* dev);
LOCAL_FN struct eegdev* egdi_create_eegdev(const struct egdi_plugin_info* info);

LOCAL_FN int egdi_update_ringbuffer(struct devmodule* mdev, const void* in, size_t length);
LOCAL_FN void egdi_report_error(struct devmodule* mdev, int error);
LOCAL_FN struct selected_channels* egdi_alloc_input_groups(struct devmodule* mdev, unsigned int ngrp);
LOCAL_FN void egdi_set_input_samlen(struct devmodule* mdev, unsigned int samlen);
LOCAL_FN const char* egdi_getopt(const char* opt, const char* def, const char* optv[]);
LOCAL_FN int egdi_split_alloc_chgroups(struct eegdev* dev,
                              unsigned int ngrp, const struct grpconf* grp);
LOCAL_FN void egdi_default_fill_chinfo(const struct eegdev*, int,
               unsigned int, struct egdi_chinfo*, struct egdi_signal_info*);
#define get_typed_val(gval, type) 			\
((type == EGD_INT32) ? gval.valint32_t : 			\
	(type == EGD_FLOAT ? gval.valfloat : gval.valdouble))

typedef void (*cast_function)(void* restrict, const void* restrict,
                              union gval, size_t);

LOCAL_FN
cast_function egd_get_cast_fn(unsigned int intypes, unsigned int outtype,
                              unsigned int scaling);
LOCAL_FN
const struct egdi_chinfo* egdi_get_conf_mapping(struct devmodule* mdev,
                                               const char* name, int* pnch);


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



// The structure containing the pointer to the methods of the EEG devices
struct eegdev_operations {
/* \param dev	pointer to the devmodule struct of the device
 *
 * Should close the device and free all associated resources.
 * egd_destroy_devmodule should be called in that method
 *
 * Should returns 0 in case of success or -1 if an error occurred (errno
 * should then be set accordingly) */
	int (*close_device)(struct devmodule* dev);


/* \param dev	pointer to the devmodule struct of the device
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
	int (*set_channel_groups)(struct devmodule* dev, unsigned int ngrp,
					const struct grpconf* grp);


/* \param dev	pointer to the devmodule struct of the device
 *
 * Called when the acquisition is about to start.
 *
 * Should returns 0 in case of success or -1 if an error occurred (errno
 * should then be set accordingly) */
	int (*start_acq)(struct devmodule* dev);


/* \param dev	pointer to the devmodule struct of the device
 *
 * Called when the acquisition is about to stop.
 *
 * Should returns 0 in case of success or -1 if an error occurred (errno
 * should then be set accordingly) */
	int (*stop_acq)(struct devmodule* dev);

/* \param dev	 	pointer to the devmodule struct of the device
 * \param stype	 	index to the sensor type
 * \param ich		index of the desired channel of the sensor type
 * \param chinfo	pointer to a egdi_chinfo struct to be filled
 * \param sinfo		pointer to a egdi_signal_info struct to be filled
 *
 * Called when the system need to know information about a particular
 * channel. */
	void (*fill_chinfo)(const struct devmodule* dev, int stype,
	                    unsigned int ich, struct egdi_chinfo* chinfo,
			    struct egdi_signal_info* siginfo);
};


struct systemcap {
	unsigned int sampling_freq;
	unsigned int nch;
	const struct egdi_chinfo* chmap;
	const char* device_type;
	const char* device_id;
};


struct eegdev {
	const struct eegdev_operations ops;
	struct systemcap cap;
	int* provided_stypes;
	int* type_nch;
	unsigned int num_stypes;
	void* auxdata;
	struct conf* cf;

	char* buffer;
	size_t buffsize, in_samlen, buff_samlen, in_offset, buff_ns;
	unsigned int ind, last_read, nreadwait;
	unsigned long ns_written, ns_read;
	mm_thr_mutex_t synclock;
	mm_thr_mutex_t apilock;
	mm_thr_cond_t available;
	int acq_order, acquiring;
	int error;

	unsigned int narr;
	size_t *strides;

	unsigned int ngrp, nsel, nconf;
	struct input_buffer_group* inbuffgrp;
	struct selected_channels* selch;
	struct array_config* arrconf;

	void* handle;
	struct devmodule module;
};

#define get_eegdev(mdev) \
  ((struct eegdev*)(((intptr_t)(mdev)) - offsetof(struct eegdev, module)))


#endif	//COREINTERNALS_H
