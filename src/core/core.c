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
#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdlib.h>
#include <errno.h>
#include <mmdlfcn.h>
#include <mmthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "eegdev-pluginapi.h"
#include "coreinternals.h"

#define BUFF_SIZE	10	//in seconds

/*******************************************************************
 *                Implementation of internals                      *
 *******************************************************************/
static int reterrno(int err)
{
	errno = err;
	return -1;
}

static
int get_device_sensorindex(const struct eegdev* dev, int stype)
{
	int i;

	for (i=0; dev->provided_stypes[i] != -1; i++)
		if (dev->provided_stypes[i] == stype)
			return i;

	return -1;
}

static
void optimize_inbufgrp(struct input_buffer_group* ibgrp, unsigned int* ngrp)
{
	unsigned int i, j, num = *ngrp;

	for (i=0; i<num; i++) {
		for (j=i+1; j<num; j++) {
			if ( (ibgrp[j].in_offset 
			            == ibgrp[i].in_offset+ibgrp[i].inlen)
			  && (ibgrp[j].buff_offset
			            == ibgrp[i].buff_offset+ibgrp[i].inlen)
			  && (ibgrp[j].sc.valdouble
			            == ibgrp[i].sc.valdouble)
			  && (ibgrp[j].cast_fn == ibgrp[i].cast_fn) ) {
				ibgrp[i].inlen += ibgrp[j].inlen;
				memmove(ibgrp + j, ibgrp + j+1,
				            (num-j-1)*sizeof(*ibgrp));
				num--;
				j--;
			}
		}
	}
	*ngrp = num;
}


static 
int setup_ringbuffer_mapping(struct eegdev* dev)
{
	unsigned int i, offset = 0;
	unsigned int isiz, bsiz, ti, tb;
	struct selected_channels* selch = dev->selch;
	struct input_buffer_group* ibgrp = dev->inbuffgrp;

	for (i=0; i<dev->nsel; i++) {
		ti = selch[i].typein;
		tb = selch[i].typeout;
		isiz = egd_get_data_size(ti);
		bsiz = egd_get_data_size(tb);
		if (isiz == 0 || bsiz == 0)
			return -1;

		// Set parameters of (input (device) -> ringbuffer)
		ibgrp[i].in_offset = selch[i].in_offset;
		ibgrp[i].inlen = selch[i].inlen;
		ibgrp[i].buff_offset = offset;
		ibgrp[i].in_tsize = isiz;
		ibgrp[i].buff_tsize = bsiz;
		ibgrp[i].sc = selch[i].sc;
		ibgrp[i].cast_fn = egd_get_cast_fn(ti, tb, selch[i].bsc);

		// Set parameters of (ringbuffer -> arrays)
		dev->arrconf[i].len = bsiz * selch[i].inlen / isiz;
		dev->arrconf[i].iarray = selch[i].iarray;
		dev->arrconf[i].arr_offset = selch[i].arr_offset;
		dev->arrconf[i].buff_offset = offset;
		offset += dev->arrconf[i].len;
	}
	dev->buff_samlen = offset;

	// Optimization should take place here
	optimize_inbufgrp(dev->inbuffgrp, &(dev->ngrp));

	return 0;
}


static
unsigned int cast_data(struct eegdev* restrict dev, 
                       const void* restrict in, size_t length)
{
	unsigned int i, ns = 0;
	const char* pi = in;
	char* restrict ringbuffer = dev->buffer;
	const struct input_buffer_group* ibgrp = dev->inbuffgrp;
	size_t offset = dev->in_offset, ind = dev->ind;
	ssize_t len, inoff, buffoff, rest, inlen = length;

	while (inlen) {
		for (i=0; i<dev->ngrp; i++) {
			len = ibgrp[i].inlen;
			inoff = ibgrp[i].in_offset - offset;
			buffoff = ibgrp[i].buff_offset;
			if (inoff < 0) {
				len += inoff;
				if (len <= 0)
					continue;
				buffoff -= ibgrp[i].buff_tsize * inoff
				              / ibgrp[i].in_tsize;
				inoff = 0;
			}
			if ((rest = inlen-inoff) <= 0)
				continue;
			len = (len <= rest) ?  len : rest;
			ibgrp[i].cast_fn(ringbuffer + ind + buffoff, 
			               pi + inoff, ibgrp[i].sc, len);
		}
		rest = dev->in_samlen - offset;
		if (inlen < rest) {
			break;
		}

		inlen -= rest;
		pi += rest;
		offset = 0;
		ns++;
		ind = (ind + dev->buff_samlen) % dev->buffsize;
	}
	dev->ind = ind;

	return ns;
}


static
int validate_groups_settings(struct eegdev* dev, unsigned int ngrp,
                                    const struct grpconf* grp)
{
	unsigned int i, stype;
	int sensind;
	
	// Groups validation
	for (i=0; i<ngrp; i++) {
		if (!grp[i].nch)
			continue;
		stype = grp[i].sensortype;
		sensind = get_device_sensorindex(dev, stype);
		if (sensind < 0
		   ||((int)(grp[i].index+grp[i].nch)>dev->type_nch[sensind])
		   ||(grp[i].datatype >= EGD_NUM_DTYPE)) 
			return reterrno(EINVAL);
	}
	
	return 0;
}


static
int wait_for_data(struct eegdev* dev, size_t* reqns)
{
	int error;
	size_t ns = *reqns;

	mm_thr_mutex_lock(&(dev->synclock));
	dev->nreadwait = ns;

	// Wait for data available or acquisition stop
	while (!(error = dev->error) && dev->acquiring
	           && (dev->ns_read + ns > dev->ns_written) )
		mm_thr_cond_wait(&(dev->available), &(dev->synclock));

	// Update data request if less can be read
	if ((error || !dev->acquiring)
	    && (dev->ns_read + *reqns > dev->ns_written))
		*reqns = dev->ns_written - dev->ns_read;
	
	dev->nreadwait = 0;
	mm_thr_mutex_unlock(&(dev->synclock));

	return error;
}

static void safe_strncpy(char* dst, const char* src, size_t n)
{
	const char* strsrc = (src != NULL) ? src : "";
	size_t eos = strlen(strsrc);

	if (eos >= n)
		eos = n-1;
	
	memcpy(dst, strsrc, eos);
	dst[eos] = '\0';
}

static
int get_field_info(struct egdi_chinfo* info, int index, int field, void* arg)
{
	const struct egdi_signal_info* si = info->si;

	if (field == EGD_LABEL) {
		if (info->label)
			safe_strncpy(arg, info->label, EGD_LABEL_LEN);
		else
			sprintf(arg, "%s:%i", egd_sensor_name(info->stype),
			                      index);
	}else if (field == EGD_ISINT)
		*((int*)arg) = si->isint;
	else if (field == EGD_MM_I) {
		*((int32_t*)arg) = get_typed_val(si->min, si->mmtype);
		*((int32_t*)arg +1) = get_typed_val(si->max, si->mmtype);
	} else if (field == EGD_MM_F) {
		*((float*)arg) = get_typed_val(si->min, si->mmtype);
		*((float*)arg +1) = get_typed_val(si->max, si->mmtype);
	} else if (field == EGD_MM_D) {
		*((double*)arg) = get_typed_val(si->min, si->mmtype);
		*((double*)arg +1) = get_typed_val(si->max, si->mmtype);
	} else if (field == EGD_UNIT) 
		safe_strncpy(arg, si->unit, EGD_UNIT_LEN);
	else if (field == EGD_TRANSDUCER) 
		safe_strncpy(arg, si->transducer, EGD_TRANSDUCER_LEN);
	else if (field == EGD_PREFILTERING) 
		safe_strncpy(arg, si->prefiltering, EGD_PREFILTERING_LEN);
	return 0;
}


static
int find_supported_sensor(struct eegdev* dev, unsigned int nch,
                          const struct egdi_chinfo* chmap)
{
	int last=-1, need_inc=1, j, ntype=0;
	int *type_nch, *types = NULL;
	unsigned int i=0;

	while (i<nch && need_inc) {
		types = realloc(types, (2*ntype+1)*sizeof(*types));
		dev->provided_stypes = types;
		if (!types)
			return -1;
		
		// Continue the scanning the map of channel
		need_inc = 0;
		for (; i<nch; i++) {
			if (chmap[i].stype == last)
				continue;

			// search sensor type in the list of previous types
			last = chmap[i].stype;
			for (j=0; j<ntype; j++)
				if (types[j] == last)
					break;

			// Notify to enlarge buffer if new type
			if (j==ntype && last!=-1) {
				need_inc = 1;
				types[ntype++] = last;
				break;
			}
		}
	}
	types[ntype] = -1;

	// Create the array of number channel per sensor type
	type_nch = types + ntype + 1;
	memset(type_nch, 0, ntype*sizeof(*type_nch));
	for (i=0; i<nch; i++) {
		last = chmap[i].stype;
		for (j=0; j<ntype; j++)
			if (types[j] == last) {
				type_nch[j]++;
				break;
			}
	}

	dev->type_nch = type_nch;
	return 0;
}


static
int validate_cap_flags(const struct plugincap* cap)
{
	int flags = cap->flags;

	if ((flags & EGDCAP_NOCP_CHMAP) &&
	        (cap->num_mappings > 1
		  || cap->mappings[0].num_skipped
		  || cap->mappings[0].default_info) )
		flags &= ~EGDCAP_NOCP_CHMAP;

	if (!(flags & EGDCAP_NOCP_CHLABEL) && (flags & EGDCAP_NOCP_CHMAP))
		flags &= ~EGDCAP_NOCP_CHMAP;

	return flags;
}


static
size_t calc_aux_capdata_size(int actual_flags, const struct plugincap* cap)
{
	const struct egdi_chinfo* chmap;
	unsigned int nch, i, j;
	int bcopylabels = !(actual_flags & EGDCAP_NOCP_CHLABEL);
	size_t auxlen = 0;

	if ( !(actual_flags & EGDCAP_NOCP_CHMAP) )
		for (i=0; i < cap->num_mappings; i++) {
			nch = cap->mappings[i].nch
			      + cap->mappings[i].num_skipped;
			auxlen += nch * sizeof(*chmap);
		}

	if ( !(actual_flags & EGDCAP_NOCP_DEVTYPE) )
		auxlen += strlen(cap->device_type) + 1;

	if ( !(actual_flags & EGDCAP_NOCP_DEVID) )
		auxlen += strlen(cap->device_id) + 1;

	for (i=0; i<cap->num_mappings && bcopylabels; i++) {
		chmap = cap->mappings[i].chmap;
		nch = cap->mappings[i].nch;

		for (j = 0; j < nch; j++)
			if (chmap[j].label)
				auxlen += strlen(chmap[j].label)+1;
	}

	return auxlen;
}


static
int fill_chmap_from_mappings(void** auxbuf, int num,
                             const struct blockmapping* mappings)
{
	struct egdi_chinfo* restrict chmap = *auxbuf;
	int i, j, nch, nch_tot = 0;

	for (i = 0; i < num; i++) {
		nch = mappings[i].nch;
		memcpy(chmap, mappings[i].chmap, nch * sizeof(*chmap));
		for (j = 0; mappings[i].default_info && (j < nch); j++)
			if (!chmap[j].si)
				chmap[j].si = mappings[i].default_info;

		for (j = 0; j < mappings[i].num_skipped; j++) {
			chmap[j + nch].stype = mappings[i].skipped_stype;
			chmap[j + nch].label = NULL;
			chmap[j + nch].si = mappings[i].default_info;
		}
		chmap += nch + mappings[i].num_skipped;
		nch_tot += nch + mappings[i].num_skipped;
	}

	*auxbuf = chmap;
	return nch_tot;
}


static
void copy_labels_in_aux(void** auxbuf, int nch, struct egdi_chinfo* chmap)
{
	int i;
	size_t len;
	char* labelbuf = *auxbuf;

	for (i = 0; i < nch; i++) {
		if (chmap[i].label) {
			len = strlen(chmap[i].label) + 1;
			memcpy(labelbuf, chmap[i].label, len);
			chmap[i].label = labelbuf;
			labelbuf += len;
		}
	}

	*auxbuf = labelbuf;
}


/*******************************************************************
 *                        Systems common                           *
 *******************************************************************/
static
int noaction(struct devmodule* dev)
{
	(void)dev;
	return 0;
}


static
int egdi_set_cap(struct devmodule* mdev, const struct plugincap* cap)
{
	int flags, nch;
	struct egdi_chinfo *chmap;
	void* auxbuff = NULL;
	size_t len;
	char* auxlabel;
	struct eegdev* dev = get_eegdev(mdev);

	flags = validate_cap_flags(cap);
	auxbuff = malloc(calc_aux_capdata_size(flags, cap));
	if (!auxbuff)
		return -1;

	dev->cap.sampling_freq = cap->sampling_freq;
	dev->cap.device_type = cap->device_type;
	dev->cap.device_id = cap->device_id;

	// Copy optionally the data to the unique auxilliary buffer
	dev->auxdata = auxbuff;
	if (!(flags & EGDCAP_NOCP_CHMAP)) {
		chmap = auxbuff;
		nch = fill_chmap_from_mappings(&auxbuff, cap->num_mappings,
                                                         cap->mappings);

		if (!(flags & EGDCAP_NOCP_CHLABEL))
			copy_labels_in_aux(&auxbuff, nch, chmap);

		dev->cap.nch = nch;
		dev->cap.chmap = chmap;
	} else {
		dev->cap.nch = cap->mappings[0].nch;
		dev->cap.chmap = cap->mappings[0].chmap;
	}

	auxlabel = auxbuff;

	if (!(flags & EGDCAP_NOCP_DEVTYPE)) {
		len = strlen(cap->device_type) + 1;
		memcpy(auxlabel, cap->device_type, len);
		dev->cap.device_type = auxlabel;
		auxlabel += len;
	}

	if (!(flags & EGDCAP_NOCP_DEVID)) {
		len = strlen(cap->device_id) + 1;
		memcpy(auxlabel, cap->device_id, len);
		dev->cap.device_id = auxlabel;
	}

	if (!dev->cap.nch
	    || find_supported_sensor(dev, dev->cap.nch, dev->cap.chmap))
		return -1;

	return 0;
}


LOCAL_FN
struct eegdev* egdi_create_eegdev(const struct egdi_plugin_info* info)
{	
	int stinit = 0;
	struct eegdev* dev;
	struct eegdev_operations ops;
	struct core_interface* ci;
	size_t dsize = info->struct_size+sizeof(*dev)-sizeof(dev->module);
	
	if (!(dev = calloc(1, dsize))
	   || mm_thr_cond_init(&(dev->available), 0) || !(++stinit)
	   || mm_thr_mutex_init(&(dev->synclock), 0) || !(++stinit)
	   || mm_thr_mutex_init(&(dev->apilock), 0))
		goto fail;

	//Register device methods
	ops.close_device = 	info->close_device;
	ops.set_channel_groups = 	info->set_channel_groups;
	ops.fill_chinfo = 		info->fill_chinfo;
	ops.start_acq = info->start_acq ? info->start_acq : noaction;
	ops.stop_acq =  info->stop_acq ? info->stop_acq : noaction;
	memcpy((void*)(&dev->ops), &ops, sizeof(ops));

	//Export core library functions needed by the plugins
	ci = (struct core_interface*) &(dev->module.ci);
	ci->update_ringbuffer = egdi_update_ringbuffer;
	ci->report_error = egdi_report_error;
	ci->alloc_input_groups = egdi_alloc_input_groups;
	ci->set_input_samlen = egdi_set_input_samlen;
	ci->set_cap = egdi_set_cap;
	ci->get_stype = egd_sensor_type;
	ci->get_conf_mapping = egdi_get_conf_mapping;

	return dev;

fail:
	if (stinit--)
		mm_thr_mutex_deinit(&(dev->synclock));
	if (stinit--)
		mm_thr_cond_deinit(&(dev->available));
	free(dev);
	return NULL;
}


LOCAL_FN
void egd_destroy_eegdev(struct eegdev* dev)
{	
	if (!dev)
		return;

	free(dev->auxdata);
	free(dev->provided_stypes);

	mm_thr_cond_deinit(&(dev->available));
	mm_thr_mutex_deinit(&(dev->synclock));
	mm_thr_mutex_deinit(&(dev->apilock));
	
	free(dev->selch);
	free(dev->inbuffgrp);
	free(dev->arrconf);
	free(dev->strides);
	free(dev->buffer);

	free(dev);
}


LOCAL_FN
int egdi_update_ringbuffer(struct devmodule* mdev, const void* in, size_t length)
{
	unsigned int ns, rest;
	int acquiring;
	size_t nsread, ns_be_written;
	struct eegdev* dev = get_eegdev(mdev);
	mm_thr_mutex_t* synclock = &(dev->synclock);

	// Process acquisition order
	mm_thr_mutex_lock(synclock);
	nsread = dev->ns_read;
	acquiring = dev->acquiring;
	if (dev->acq_order == EGD_ORDER_START) {
		// Check if we can start the acquisition now. If not
		// postpone it to a later call of update_ringbuffer, i.e. do
		// not reset the order
		rest = (dev->in_samlen - dev->in_offset) % dev->in_samlen;
		if (rest <= length) {
			dev->acq_order = EGD_ORDER_NONE;

			// realign on beginning of the next sample
			// (avoid junk at the beginning of the acquisition)
			in = (char*)in + rest;
			length -= rest;
			dev->in_offset = 0;
		}
	} else if (dev->acq_order == EGD_ORDER_STOP) {
		dev->acq_order = EGD_ORDER_NONE;
		acquiring = dev->acquiring = 0;
	}
	mm_thr_mutex_unlock(synclock);

	if (acquiring) {
		// Test for ringbuffer full
		ns_be_written = length/dev->in_samlen + 2 + dev->ns_written;
		if (ns_be_written - nsread >= dev->buff_ns) {
			egdi_report_error(mdev, ENOMEM);
			return -1;
		}

		// Put data on the ringbuffer
		ns = cast_data(dev, in, length);

		// Update number of sample available and signal if
		// thread is waiting for data
		mm_thr_mutex_lock(synclock);
		dev->ns_written += ns;
		if (dev->nreadwait
		   && (dev->nreadwait + dev->ns_read <= dev->ns_written))
			mm_thr_cond_signal(&(dev->available));
		mm_thr_mutex_unlock(synclock);
	}

	dev->in_offset = (length + dev->in_offset) % dev->in_samlen;
	return 0;
}


LOCAL_FN
void egdi_report_error(struct devmodule* mdev, int error)
{
	struct eegdev *dev = get_eegdev(mdev);
	mm_thr_mutex_lock(&dev->synclock);

	if (!dev->error)
		dev->error = error;
	
	if (dev->nreadwait)
		mm_thr_cond_signal(&(dev->available));

	mm_thr_mutex_unlock(&dev->synclock);
}


LOCAL_FN
struct selected_channels* egdi_alloc_input_groups(struct devmodule* mdev,
                                                 unsigned int ngrp)
{
	struct eegdev* dev = get_eegdev(mdev);

	free(dev->selch);
	free(dev->inbuffgrp);
	free(dev->arrconf);

	// Alloc ringbuffer mapping structures
	dev->nsel = dev->nconf = dev->ngrp = ngrp;
	dev->selch = calloc(ngrp,sizeof(*(dev->selch)));
	dev->inbuffgrp = calloc(ngrp,sizeof(*(dev->inbuffgrp)));
	dev->arrconf = calloc(ngrp,sizeof(*(dev->arrconf)));
	if (!dev->selch || !dev->inbuffgrp || !dev->arrconf)
		return NULL;
	
	return dev->selch;
}


LOCAL_FN
void egdi_set_input_samlen(struct devmodule* mdev, unsigned int samlen)
{
	get_eegdev(mdev)->in_samlen = samlen;
}

/*******************************************************************
 *                    API functions implementation                 *
 *******************************************************************/
/**
 * egd_get_cap() - queries various information about the device capabilities
 * @dev: reference to a device
 * @cap: capability whose information are retrieved
 * @val: value filled with the requested information
 *
 * egd_get_cap() returns the information about the capability specified
 * by @cap of the recording device referenced by @dev by the mean of
 * the pointer @val.
 *
 * The type of pointer passed through @val is dependent on the requested
 * capability @cap which can be one the following (The expected type
 * pointed by @val is provided in parenthesis):
 *
 * EGD_CAP_FS ( int )
 *   Sampling frequency that is currently set. The same value is returned by
 *   the function.
 *
 * EGD_CAP_TYPELIST ( const int * )
 *   Array of sensor types sampled by the device terminated by -1. These values
 *   became valid as argument for egd_sensor_name() when opening the
 *   device @dev if they were not yet before. The number of elements in
 *   the array (excluding the -1 element) is provided by the return value of
 *   the function.
 *
 * EGD_CAP_DEVTYPE ( const char * )
 *   Null terminated string describing the type of the recording device
 *   (Manufacturer and model if @dev refers to actual hardware). The length
 *   of string (excluding the null character) is provided by the return value
 *   of the function.
 *
 * EGD_CAP_DEVID ( const char * )
 *   Null-terminated string describing the model of the recording device (or an
 *   unique identifier of the resource). The length of string (excluding the
 *   null character) is provided by the return value of the function.
 *
 * Return:
 * In case of success, the function returns a positive value depending on the
 * requested capability. Otherwise, -1 is returned and errno is set
 * accordingly.
 *
 * Errors:
 * EINVAL
 *   @dev is NULL, @cap is not valid value or @val is NULL while
 *   @cap is not EGD_CAP_FS.
 */
API_EXPORTED
int egd_get_cap(const struct eegdev* dev, int cap, void* val)
{
	int retval = 0;

	if (dev == NULL || (cap != EGD_CAP_FS && val == NULL))
		return reterrno(EINVAL);

	switch (cap) {
	case EGD_CAP_FS:
		if (val != NULL)
			*(unsigned int*)val = dev->cap.sampling_freq;
		retval = (int)dev->cap.sampling_freq;
		break;

        case EGD_CAP_TYPELIST:
		*(const int**)val = dev->provided_stypes;
		retval = dev->num_stypes;
		break;

        case EGD_CAP_DEVTYPE:
		*(const char**)val = dev->cap.device_type;
		retval = strlen(dev->cap.device_type);
		break;

        case EGD_CAP_DEVID:
		*(const char**)val = dev->cap.device_id;
		retval = strlen(dev->cap.device_id);
		break;

        default:
		retval = -1;
		errno = EINVAL;
	}

	return retval;
}


/**
 * egd_get_numch() - gets the number of channels sampled by type
 * @dev: pointer to a device
 * @stype: type of the channels whose number is requested
 *
 * egd_get_numch() returns the number of channels of type @stype
 * sampled by the recording device referenced by @dev. @stype must be
 * one of the values returned by egd_sensor_type().
 *
 * Return:
 * In case of success, the function returns a non negative value corresponding
 * to the number of channels of type @stype (possibly 0 if the device does
 * not sample this type of data). Otherwise, -1 is returned and errno is
 * set accordingly.
 *
 * Errors:
 * EINVAL
 *   @dev is NULL or @stype is not valid value.
 */
API_EXPORTED
int egd_get_numch(const struct eegdev* dev, int stype)
{
	int itype;

	if (dev == NULL)
		return reterrno(EINVAL);

	if ((itype=get_device_sensorindex(dev, stype)) < 0)
		return 0;
	
	return dev->type_nch[itype];
}


/**
 * egd_channel_info() - gets various information about a particular channel
 * @dev: reference to a device
 * @stype: type of the channel whose information are retrieved
 * @index: index of the channel whose information are retrieved
 * @fieldtype: type of the information to retrieve
 *
 * egd_channel_info() provides different type of information about the
 * channel at the index @index of the group specified by @stype of
 * the device referenced by @dev. As described for egd_acq_setup(),
 * @stype specifies the type of channel. It must one of the values
 * returned by egd_sensor_type().
 *
 * The information returned by the function is defined by the variable list of
 * argument. This list is composed of successive couple grouping one field type
 * identifier specifying the feature to be get and a pointer to a value whose
 * type depends on the previous field type. The list MUST finish by
 * EGD_END.
 *
 * The field identifiers can be the following (The expected corresponding
 * pointer type is provided in parenthesis):
 *
 * EGD_LABEL ( char * )
 *   Name of the channel. The pointed array should be long enough to hold 32
 *   characters (including the null termination character).
 *
 * EGD_ISINT ( int * )
 *   Indicates whether the data provided by the channel are integer or floating
 *   point. (zero indicates floating point, non-zero indicates integer).
 *
 * EGD_MM_I ( int32_t * )
 *   Returns in an array of 2 int32_t the minimal and maximal values that
 *   the channel can deliver (If the channel deliver floating point data, these
 *   values can be underestimated due to overflow)
 *
 * EGD_MM_F ( float * )
 *   Returns in an array of 2 float values the minimal and maximal values
 *   that the channel can deliver (If the channel deliver double floating point
 *   data, these values can be underestimated due to overflow)
 *
 * EGD_MM_D ( double * )
 *   Returns in an array of 2 double values the minimal and maximal values
 *   that the channel can deliver.
 *
 * EGD_UNIT ( char * )
 *   Unit in which the channel data is expressed. The pointed array should be
 *   long enough to hold 16 characters (including the null termination
 *   character).
 *
 * EGD_TRANSDUCER ( char * )
 *   Transducer type of the sensor. The pointed array should be long enough to
 *   hold 128 characters (including the null termination character).
 *
 * EGD_PREFILTERING ( char * )
 *   Information about the filters already applied on data. The pointed array
 *   should be long enough to hold 128 characters (including the null
 *   termination character).
 *
 * egd_channel_info() is thread-safe.
 *
 * Return:
 * The function returns 0 in case of success. Otherwise, -1 is returned
 * and errnois set accordingly.
 *
 * Errors:
 * EINVAL
 * @dev is NULL, @stype is an invalid sensor type, @index is
 * bigger than the maximal number of channel in the group, any field identifier
 * is unknown or any pointer used is NULL.
 */
API_EXPORTED
int egd_channel_info(const struct eegdev* dev, int stype,
                     unsigned int index, int fieldtype, ...)
{
	va_list ap;
	int field, itype, retval = 0;
	void* arg;
	struct egdi_signal_info sinfo = {.unit = NULL};
	struct egdi_chinfo chinfo = {.si = &sinfo};
	mm_thr_mutex_t* apilock = (mm_thr_mutex_t*)&(dev->apilock);

	// Argument validation
	if (dev == NULL
	  || (itype = get_device_sensorindex(dev, stype)) < 0
	  || (int)index >= dev->type_nch[itype])
		return reterrno(EINVAL);

	mm_thr_mutex_lock(apilock);

	// Get channel info from the backend
	egdi_default_fill_chinfo(dev, stype, index, &chinfo, &sinfo);
	if (dev->ops.fill_chinfo)
		dev->ops.fill_chinfo(&dev->module, stype, index, &chinfo, &sinfo);

	// field parsing
	va_start(ap, fieldtype);
	field = fieldtype;
	while (field != EGD_EOL && !retval) {
		if (field < 0 || field >= EGD_NUM_FIELDS
		   || ((arg = va_arg(ap, void*)) == NULL)) {
			retval = reterrno(EINVAL);
			break;
		}
		retval = get_field_info(&chinfo, index, field, arg);
		field = va_arg(ap, int);
	}
	va_end(ap);

	mm_thr_mutex_unlock(apilock);

	return retval;
}


/**
 * egd_close() - closes a device
 * @dev: device to close
 *
 * egd_close() frees all resources associated to the device referenced by
 * @dev and closes it. It stops also implicitly any running acquisition. If
 * @dev is NULL, no operation is done and the function will silently return.
 *
 * After a successful call to egd_close(), the reference @dev cannot
 * be used any longer.
 *
 * Return:
 * The function always returns 0. It currently cannot fails but a value is
 * returned to accomodate possible future change.
 */
API_EXPORTED
int egd_close(struct eegdev* dev)
{
	int acquiring;

	if (!dev)
		return 0;

	mm_thr_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	mm_thr_mutex_unlock(&(dev->synclock));
	if (acquiring)
		egd_stop(dev);

	dev->ops.close_device(&dev->module);
	mm_dlclose(dev->handle);
	egd_destroy_eegdev(dev);

	return 0;
}


/**
 * egd_acq_setup() - specifies which and how channel data should be obtained
 * @dev: reference to the device that provide data
 * @narr: number of arrays
 * @strides: array whose values indicate the strides of the @narr arrays
 * @ngrp: number of groups of consecutive channels to be returned
 * @grp: characteristics of the groups of channels that must be returned
 *
 * egd_acq_setup() configures the way data is provided by the device
 * referenced by @dev for the next calls to egd_get_data().
 *
 * The @narr argument specifies the number of buffers that will be
 * supplied in the argument list of egd_get_data(). @strides should
 * points to an array of @narr values specifying respectively for each
 * buffers its stride, i.e. the size in bytes between the data of two
 * successive samples in this buffer.
 *
 * The channels data that must be returned in those buffers are defined by
 * @ngrp groups of consecutive channels. The groups definition are passed
 * by @grp which points to an array of @ngrp structures, defined as
 * follows :
 *
 * .. code-block:: c
 *
 *    struct grpconf {
 *       unsigned int sensortype;    // type of channel
 *       unsigned int index;         // index of the first channel
 *       unsigned int nch;           // number of channels
 *       unsigned int iarray;        // index of the array
 *       unsigned int arr_offset;    // offset in the array
 *       unsigned int datatype;      // type in the array
 *    };
 *
 * The different fields in the structure defines the properties of the group :
 *
 * - sensortype specifies the type of channel. it must one of the following
 *   values returned by egd_sensor_type().
 *
 * - index indicates the index of the first channel in the group. Note that
 *   channel index i refers the i-th channel of the type specified previously,
 *   i.e. the channel index i refers to two different channels if
 *   sensortype differs.
 *
 * - nch specifies the number of consecutive channels that should be in the
 *   group.
 *
 * - iarray indicates which buffer the data of the channel group must be
 *   written to.
 *
 * - arr_offset defines the offset of the memory location of the data in
 *   the buffers relatively to the beginning of each sample.
 *
 * - datatype specifies the type of data that must be written to the
 *   buffer. It must be one of the following value: EGD_INT32,
 *   EGD_FLOAT or EGD_DOUBLE.
 *
 * egd_acq_setup() is thread-safe.
 *
 * Return:
 * The function returns 0 in case of success. Otherwise, -1 is returned
 * and errno is set accordingly.
 *
 * Errors:
 * EINVAL
 *   @dev is NULL.
 *
 * EPERM
 *   The acquisition is running
 *
 * Example:
 * See egd_get_data() for an example
 */
API_EXPORTED
int egd_acq_setup(struct eegdev* dev, 
                  unsigned int narr, const size_t *strides,
		  unsigned int ngrp, const struct grpconf *grp)
{
	int acquiring, ret, retval = -1;

	if (!dev || (ngrp && !grp) || (narr && !strides)) 
		return reterrno(EINVAL);
	
	mm_thr_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	mm_thr_mutex_unlock(&(dev->synclock));
	if (acquiring)
		return reterrno(EPERM);

	mm_thr_mutex_lock(&(dev->apilock));

	if (validate_groups_settings(dev, ngrp, grp))
		goto out;
	
	// Alloc transfer configuration structs
	free(dev->strides);
	dev->strides = malloc(narr*sizeof(*strides));
	if ( !dev->strides)
		goto out;

	// Update arrays details
	dev->narr = narr;
	memcpy(dev->strides, strides, narr*sizeof(*strides));

	// Setup transfer configuration (this call affects ringbuffer size)
	if (dev->ops.set_channel_groups)
		ret = dev->ops.set_channel_groups(&dev->module, ngrp, grp);
	else
		ret = egdi_split_alloc_chgroups(dev, ngrp, grp);
	if (ret)
		goto out;

	retval = setup_ringbuffer_mapping(dev);
	if (retval < 0)
		goto out;

	// Alloc ringbuffer
	free(dev->buffer);
	dev->buff_ns = BUFF_SIZE*dev->cap.sampling_freq;
	dev->buffsize = BUFF_SIZE*dev->cap.sampling_freq * dev->buff_samlen;
	dev->buffer = malloc(dev->buffsize);
	if (!dev->buffer)
		goto out;
	
	retval = 0;

out:
	mm_thr_mutex_unlock(&(dev->apilock));
	return retval;
}


/**
 * egd_get_data() - peeks buffered data
 * @dev: pointer to a device
 * @ns: number of samples to retrieve
 *
 * egd_get_data() peeks the @ns next samples from the buffered data
 * acquired by the device referenced by @dev and fills the arrays
 * provided in the variable list of arguments with the obtained data. If all
 * requested samples have been already acquired, the function returns
 * immediately. Otherwise, the call blocks until the requested data is
 * available, the acquisition stops or a problem occurs. In the last two cases,
 * the data read may be less than requested.
 *
 * The arrays provided in the variable list of argument are filled following
 * the formats specified by previous call to egd_acq_setup(). In
 * particular, the number of arrays supplied in the variable list of argument
 * and their size should be consistent with the number of arrays and strides
 * specified by the call to egd_acq_setup().
 *
 * Please be aware that the user has no obligation to make all the calls to
 * egd_get_data() during the acquisition. He can also perform some of them
 * after the acquisition which will correspond to get the remaining buffered
 * data.
 *
 * For example, it might happened that a user want to wait for an certain
 * external event to occur before stopping the acquisition. In this situation,
 * the usual workflow would be to start the acquisition, get regularly some
 * data while scanning the event to occur. When this happens, the acquisition
 * is immediately stopped. However at the moment of stopping the acquisition,
 * there might still be some buffered data which could be important.
 * Calling egd_get_available() after egd_stop() would then return
 * the size of the remaining data that could be obtained with
 * egd_get_data().
 *
 * Return:
 * In case of success, egd_get_data() returns the number of read samples
 * (which can be less than the requested number). Otherwise, -1 is returned
 * and errno is set accordingly.
 *
 * Errors:
 * EINVAL
 *   @dev is NULL
 *
 * ENOMEM
 *   The internal ring buffer of the device referenced by @dev is full
 *
 * EAGAIN
 *   The underlying hardware referenced by @dev has encountered a loss of
 *   connection, maybe due some cable disconnected or a power switch set to off
 *
 * EIO
 *   The underlying hardware referenced by @dev has encountered a loss of
 *   synchronization for an unknown reason
 *
 *
 * Example:
 *
 *    #define NEEG	8
 *    #define NTRI	1
 *    #define NS	4
 *
 *    int ns_tot;
 *    ssize_t ns_read;
 *    float eegarr[NEEG * NS];
 *    int32_t triarr[NTRI * NS];
 *    struct grpconf grp[2];
 *    unsigned int strides[2];
 *
 *    // Assume that a device has been successfully opened, i.e. there
 *    // is a valid 'dev' variable of type struct eegdev *
 *
 *    strides[0] = NEEG*sizeof(float);
 *    strides[1] = NTRI*sizeof(int32_t);
 *
 *    grp[0].sensortype = egd_sensor_type("eeg");
 *    grp[0].index = 0;
 *    grp[0].iarray = 0;
 *    grp[0].arr_offset = 0;
 *    grp[0].nch = NEEG;
 *    grp[0].datatype = EGD_FLOAT;
 *    grp[1].sensortype = egd_sensor_type("trigger");
 *    grp[1].index = 0;
 *    grp[1].iarray = 1;
 *    grp[1].arr_offset = 0;
 *    grp[1].nch = NTRI;
 *    grp[1].datatype = EGD_INT32;
 *
 *    // Setup how to get the data
 *    egd_acq_setup(dev, 2, strides, 2, grp);
 *
 *    // Start the acquisition.
 *    // From now, all incoming samples will be buffered
 *    egd_start(dev);
 *    ns_tot = 0;
 *
 *    while (ns_tot < 1000) {
 *         //Get the data
 *         ns_read = egd_get_data(dev, NS, eegarr, triarr);
 *         if (ns_read < 0) {
 *              // Handle failure
 *         }
 *         ns_tot += ns_read;
 *
 *         // do something with the new data
 *    }
 *
 *    // Stop the acquisition, i.e. no new data is buffered
 *    egd_stop(dev);
 */
API_EXPORTED
ssize_t egd_get_data(struct eegdev* dev, size_t ns, ...)
{
	if (!dev)
		return reterrno(EINVAL);

	unsigned int i, s, iarr, curr_s = dev->last_read;
	struct array_config* restrict ac = dev->arrconf;
	char* restrict ringbuffer = dev->buffer;
	unsigned int narr = dev->narr;
	char* restrict buffout[narr];
	va_list ap;
	int error;

	va_start(ap, ns);
	for (i=0; i<narr; i++)
		buffout[i] = va_arg(ap, char*);
	va_end(ap);

	// Wait until there is enough data in ringbuffer or the acquisition
	// stops. If the acquisition is stopped, the number of sample read
	// MAY be smaller than requested
	error = wait_for_data(dev, &ns);
	if ((ns == 0) && error)
		return reterrno(error);

	// Copy data from ringbuffer to arrays
	for (s=0; s<ns; s++) {
		for (i=0; i<dev->nconf; i++) {
			iarr = ac[i].iarray;
			memcpy(buffout[iarr] + ac[i].arr_offset,
			       ringbuffer + curr_s + ac[i].buff_offset,
			       ac[i].len);
		}

		curr_s = (curr_s + dev->buff_samlen) % dev->buffsize;
		for (i=0; i<narr; i++)
			buffout[i] += dev->strides[i];
	}

	// Update the reading status
	mm_thr_mutex_lock(&(dev->synclock));
	dev->ns_read += ns;
	mm_thr_mutex_unlock(&(dev->synclock));

	dev->last_read = curr_s;
	return ns;
}


/**
 * egd_get_available() - gets the number of unread samples of a given device
 * @dev: pointer to a device
 *
 * egd_get_available() returns the number of samples that have been
 * buffered by the device referenced by @dev and that have not been read
 * yet.
 *
 * Please be aware that the user has no obligation to make all the calls to
 * egd_get_available() during the acquisition. He can also perform some of
 * them after the acquisition which will correspond to get the remaining
 * buffered data.
 *
 * For example, it might happened that a user want to wait for an certain
 * external event to occur before stopping the acquisition. In this situation,
 * the usual workflow would be to start the acquisition, get regularly some
 * data while scanning the event to occur. When this happens, the acquisition
 * is immediately stopped. However at the moment of stopping the acquisition,
 * there might still be some buffered data which could be important.
 * Calling egd_get_available() after egd_stop() would then return the size of
 * the remaining data that could be obtained with egd_get_data().
 *
 * Return:
 * the number of unread samples in case of success. Otherwise, -1 is
 * returned and errno is set accordingly.
 *
 * Errors:
 * EINVAL
 *   @dev is NULL
 *
 * ENOMEM
 *   The internal ring buffer of the device referenced by @dev is full
 *
 * EAGAIN
 *   The underlying hardware referenced by @dev has encountered a loss of
 *   connection, maybe due some cable disconnected or a power switch set to off
 *
 * EIO
 *   The underlying hardware referenced by @dev has encountered a loss of
 *   synchronization for an unknown reason.
 */
API_EXPORTED
ssize_t egd_get_available(struct eegdev* dev)
{
	int ns, error;

	if (!dev)
		return reterrno(EINVAL);

	mm_thr_mutex_lock(&(dev->synclock));
	ns = dev->ns_written - dev->ns_read;
	error = dev->error;
	mm_thr_mutex_unlock(&(dev->synclock));

	if (!ns && error)
		return reterrno(error);

	return ns;
}


/**
 * egd_start() - starts buffered acquisition
 * @dev: pointer to a device
 *
 * egd_start() marks the beginning of buffered acquisition from the
 * device referenced by @dev. This means that the data starts getting
 * accumulated in an internal ring buffer and this buffered data can be
 * sequentially obtained by successive calls to egd_get_data(). A
 * buffered acquisition started implies that the user has to get the data
 * often enough to prevent the situation of a full ring buffer.
 *
 * Return:
 * The function returns 0 in case of success. Otherwise, -1 is returned and
 * errno is set accordingly.
 *
 * Errors:
 * EINVAL
 *   @dev is NULL.
 */
API_EXPORTED
int egd_start(struct eegdev* dev)
{
	int acquiring;

	if (!dev)
		return reterrno(EINVAL);

	mm_thr_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	mm_thr_mutex_unlock(&(dev->synclock));
	if (acquiring)
		return reterrno(EPERM);
	
	mm_thr_mutex_lock(&(dev->synclock));
	dev->ns_read = dev->ns_written = 0;
	dev->ops.start_acq(&dev->module);

	dev->acq_order = EGD_ORDER_START;
	dev->acquiring = 1;
	mm_thr_mutex_unlock(&(dev->synclock));

	return 0;
}


/**
 * egd_stop() - stops buffered acquisition
 * @dev: pointer to a device
 *
 * egd_stop() halts the buffered acquisition of the device referenced
 * by @dev, relieving the user from the obligation to get data regularly.
 *
 * Return:
 * The function returns 0 in case of success. Otherwise, -1 is returned and
 * errno is set accordingly.
 *
 * Errors:
 * EINVAL
 *   @dev is NULL.
 */
API_EXPORTED
int egd_stop(struct eegdev* dev)
{
	int acquiring;

	if (!dev)
		return reterrno(EINVAL);

	mm_thr_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	mm_thr_mutex_unlock(&(dev->synclock));
	if (!acquiring)
		return reterrno(EPERM);

	mm_thr_mutex_lock(&(dev->synclock));
	dev->acq_order = EGD_ORDER_STOP;
	mm_thr_mutex_unlock(&(dev->synclock));

	dev->ops.stop_acq(&dev->module);
	return 0;
}


static char eegdev_string[] = PACKAGE_STRING;

/**
 * egd_get_string() - gets the name and version of the project
 *
 * Return:
 * A string that contains the name and the version of the project
 */
API_EXPORTED
const char* egd_get_string(void)
{
	return eegdev_string;	
}
