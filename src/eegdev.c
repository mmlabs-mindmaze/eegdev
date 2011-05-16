/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include "eegdev-common.h"

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
int assign_groups(struct eegdev* dev, unsigned int ngrp,
                  const struct grpconf* grp)
{
	unsigned int i, offset = 0;
	unsigned int isiz, bsiz, ti, tb;
	struct selected_channels* selch = dev->selch;
	struct input_buffer_group* ibgrp = dev->inbuffgrp;
		
	for (i=0; i<ngrp; i++) {
		ti = selch[i].typein;
		tb = grp[i].datatype;
		isiz = egd_get_data_size(ti);
		bsiz = egd_get_data_size(tb);
		ibgrp[i].in_offset = selch[i].in_offset;
		ibgrp[i].inlen = selch[i].inlen;
		ibgrp[i].buff_offset = offset;
		ibgrp[i].in_tsize = isiz;
		ibgrp[i].buff_tsize = bsiz;
		ibgrp[i].sc = selch[i].sc;
		ibgrp[i].cast_fn = egd_get_cast_fn(ti, tb, selch[i].bsc);

		// Set parameters of (ringbuffer -> arrays)
		dev->arrconf[i].len = bsiz * selch[i].inlen / isiz;
		dev->arrconf[i].iarray = grp[i].iarray;
		dev->arrconf[i].arr_offset = grp[i].arr_offset;
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
	
	// Groups validation
	for (i=0; i<ngrp; i++) {
		stype = grp[i].sensortype;
		if ((stype >= EGD_NUM_STYPE)
		    || (grp[i].index+grp[i].nch > dev->cap.type_nch[stype])
		    || (grp[i].datatype >= EGD_NUM_DTYPE)) 
			return reterrno(EINVAL);
	}
	
	return 0;
}


static
int wait_for_data(struct eegdev* dev, size_t* reqns)
{
	int error;
	size_t ns = *reqns;

	pthread_mutex_lock(&(dev->synclock));
	dev->nreadwait = ns;

	// Wait for data available or acquisition stop
	while (!(error = dev->error) && dev->acquiring
	           && (dev->ns_read + ns > dev->ns_written) )
		pthread_cond_wait(&(dev->available), &(dev->synclock));

	// Update data request if less can be read
	if ((error || !dev->acquiring)
	    && (dev->ns_read + *reqns > dev->ns_written))
		*reqns = dev->ns_written - dev->ns_read;
	
	dev->nreadwait = 0;
	pthread_mutex_unlock(&(dev->synclock));

	return error;
}

static void safe_strncpy(char* dst, const char* src, size_t n)
{
	const char* strsrc = (src != NULL) ? src : "";
	size_t eos = strlen(strsrc);

	if (eos >= n)
		eos = n-1;
	
	memcpy(dst, src, eos);
	dst[eos] = '\0';
}

static
int get_field_info(struct egd_chinfo* info, int field, void* arg)
{
	if (field == EGD_LABEL)
		safe_strncpy(arg, info->label, EGD_LABEL_LEN);
	else if (field == EGD_ISINT)
		*((int*)arg) = info->isint;
	else if (field == EGD_MM_I) {
		*((int32_t*)arg) = get_typed_val(info->min, info->dtype);
		*((int32_t*)arg +1) = get_typed_val(info->max, info->dtype);
	} else if (field == EGD_MM_F) {
		*((float*)arg) = get_typed_val(info->min, info->dtype);
		*((float*)arg +1) = get_typed_val(info->max, info->dtype);
	} else if (field == EGD_MM_D) {
		*((double*)arg) = get_typed_val(info->min, info->dtype);
		*((double*)arg +1) = get_typed_val(info->max, info->dtype);
	} else if (field == EGD_UNIT) 
		safe_strncpy(arg, info->unit, EGD_UNIT_LEN);
	else if (field == EGD_TRANSDUCTER) 
		safe_strncpy(arg, info->transducter, EGD_TRANSDUCTER_LEN);
	else if (field == EGD_PREFILTERING) 
		safe_strncpy(arg, info->prefiltering, EGD_PREFILTERING_LEN);
	return 0;
}

/*******************************************************************
 *                        Systems common                           *
 *******************************************************************/
LOCAL_FN
int egd_init_eegdev(struct eegdev* dev, const struct eegdev_operations* ops)
{	
	int ret;

	memset(dev, 0, sizeof(*dev));

	ret = pthread_cond_init(&(dev->available), NULL);
	if (ret)
		return reterrno(ret);

	ret = pthread_mutex_init(&(dev->synclock), NULL);
	if (ret) {
		pthread_cond_destroy(&(dev->available));
		return reterrno(ret);
	}

	ret = pthread_mutex_init(&(dev->apilock), NULL);
	if (ret) {
		pthread_mutex_destroy(&(dev->synclock));
		pthread_cond_destroy(&(dev->available));
		return reterrno(ret);
	}

	memcpy((void*)&(dev->ops), ops, sizeof(*ops));

	return 0;
}


LOCAL_FN
void egd_destroy_eegdev(struct eegdev* dev)
{	
	// If methods have not been initialized, the structure has failed
	// in its initialization. 
	if (dev->ops.close_device == NULL)
		return;

	pthread_cond_destroy(&(dev->available));
	pthread_mutex_destroy(&(dev->synclock));
	pthread_mutex_destroy(&(dev->apilock));
	
	free(dev->selch);
	free(dev->inbuffgrp);
	free(dev->arrconf);
	free(dev->strides);
	free(dev->buffer);
}


LOCAL_FN
int egd_update_ringbuffer(struct eegdev* dev, const void* in, size_t length)
{
	unsigned int ns, rest;
	int acquiring;
	size_t nsread, ns_be_written;
	pthread_mutex_t* synclock = &(dev->synclock);

	// Process acquisition order
	pthread_mutex_lock(synclock);
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
	pthread_mutex_unlock(synclock);

	if (acquiring) {
		// Test for ringbuffer full
		ns_be_written = length/dev->in_samlen + 2 + dev->ns_written;
		if (ns_be_written - nsread >= dev->buff_ns) {
			egd_report_error(dev, ENOMEM);
			return -1;
		}

		// Put data on the ringbuffer
		ns = cast_data(dev, in, length);

		// Update number of sample available and signal if
		// thread is waiting for data
		pthread_mutex_lock(synclock);
		dev->ns_written += ns;
		if (dev->nreadwait
		   && (dev->nreadwait + dev->ns_read <= dev->ns_written))
			pthread_cond_signal(&(dev->available));
		pthread_mutex_unlock(synclock);
	}

	dev->in_offset = (length + dev->in_offset) % dev->in_samlen;
	return 0;
}


LOCAL_FN
void egd_report_error(struct eegdev* dev, int error)
{
	pthread_mutex_lock(&dev->synclock);

	if (!dev->error)
		dev->error = error;
	
	if (dev->nreadwait)
		pthread_cond_signal(&(dev->available));

	pthread_mutex_unlock(&dev->synclock);
}


LOCAL_FN
void egd_update_capabilities(struct eegdev* dev)
{
	int stype;
	unsigned int num_stypes = 0;

	for (stype=0; stype<EGD_NUM_STYPE; stype++)
		if (dev->cap.type_nch[stype] > 0)
			dev->provided_stypes[num_stypes++] = stype;
	dev->provided_stypes[num_stypes] = -1;
	dev->num_stypes = num_stypes;
}


LOCAL_FN
struct selected_channels* egd_alloc_input_groups(struct eegdev* dev,
                                                 unsigned int ngrp)
{
	free(dev->selch);
	free(dev->inbuffgrp);

	dev->nsel = dev->ngrp = ngrp;
	dev->selch = calloc(ngrp,sizeof(*(dev->selch)));
	dev->inbuffgrp = calloc(ngrp,sizeof(*(dev->inbuffgrp)));
	if (!dev->selch || !dev->inbuffgrp)
		return NULL;
	
	return dev->selch;
}


LOCAL_FN
void egd_set_input_samlen(struct eegdev* dev, unsigned int samlen)
{
	dev->in_samlen = samlen;
}


LOCAL_FN
const char* egd_getopt(const char* opt, const char* def, const char* optv[])
{
	int i = 0;
	while (optv[i]) {
		if (!strcmp(opt, optv[i]))
			return optv[i+1];
		i += 2;
	}
	return def;
}

/*******************************************************************
 *                    API functions implementation                 *
 *******************************************************************/
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


API_EXPORTED
int egd_get_numch(const struct eegdev* dev, int stype)
{
	if (dev == NULL || stype < 0 || stype >= EGD_NUM_STYPE)
		return reterrno(EINVAL);
	
	return dev->cap.type_nch[stype];
}


API_EXPORTED
int egd_channel_info(const struct eegdev* dev, int stype,
                     unsigned int index, int fieldtype, ...)
{
	va_list ap;
	const unsigned int* nmax;
	int field, retval = 0;
	void* arg;
	struct egd_chinfo chinfo = {.label = NULL};
	pthread_mutex_t* apilock = (pthread_mutex_t*)&(dev->apilock);

	// Argument validation
	if (dev == NULL)
		return reterrno(EINVAL);
	nmax = dev->cap.type_nch;
	if (stype < 0 || stype >= EGD_NUM_STYPE || index >= nmax[stype])
		return reterrno(EINVAL);

	pthread_mutex_lock(apilock);

	// Get channel info from the backend
	assert(dev->ops.fill_chinfo);
	dev->ops.fill_chinfo(dev, stype, index, &chinfo);

	// field parsing
	va_start(ap, fieldtype);
	field = fieldtype;
	while (field != EGD_EOL && !retval) {
		if (field < 0 || field >= EGD_NUM_FIELDS
		   || ((arg = va_arg(ap, void*)) == NULL)) {
			retval = reterrno(EINVAL);
			break;
		}
		retval = get_field_info(&chinfo, field, arg);
		field = va_arg(ap, int);
	}
	va_end(ap);

	pthread_mutex_unlock(apilock);

	return retval;
}

API_EXPORTED
int egd_close(struct eegdev* dev)
{
	int acquiring;
	if (!dev)
		return reterrno(EINVAL);

	pthread_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	pthread_mutex_unlock(&(dev->synclock));
	if (acquiring)
		egd_stop(dev);

	dev->ops.close_device(dev);
	return 0;
}


API_EXPORTED
int egd_acq_setup(struct eegdev* dev, 
                  unsigned int narr, const size_t *strides,
		  unsigned int ngrp, const struct grpconf *grp)
{
	int acquiring, retval = -1;

	if (!dev || (ngrp && !grp) || (narr && !strides)) 
		return reterrno(EINVAL);
	
	pthread_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	pthread_mutex_unlock(&(dev->synclock));
	if (acquiring)
		return reterrno(EPERM);

	pthread_mutex_lock(&(dev->apilock));

	if (validate_groups_settings(dev, ngrp, grp))
		goto out;
	
	// Alloc transfer configuration structs
	free(dev->strides);
	free(dev->arrconf);
	dev->strides = malloc(narr*sizeof(*strides));
	dev->arrconf = calloc(ngrp,sizeof(*(dev->arrconf)));
	if (!dev->arrconf || !dev->strides)
		goto out;
	dev->nconf = ngrp;

	// Update arrays details
	dev->narr = narr;
	memcpy(dev->strides, strides, narr*sizeof(*strides));

	// Setup transfer configuration (this call affects ringbuffer size)
	if (dev->ops.set_channel_groups(dev, ngrp, grp))
		goto out;
	assign_groups(dev, ngrp, grp);

	// Alloc ringbuffer
	free(dev->buffer);
	dev->buff_ns = BUFF_SIZE*dev->cap.sampling_freq;
	dev->buffsize = BUFF_SIZE*dev->cap.sampling_freq * dev->buff_samlen;
	dev->buffer = malloc(dev->buffsize);
	if (!dev->buffer)
		goto out;
	
	retval = 0;

out:
	pthread_mutex_unlock(&(dev->apilock));
	return retval;
}


API_EXPORTED
ssize_t egd_get_data(struct eegdev* dev, size_t ns, ...)
{
	if (!dev)
		return reterrno(EINVAL);

	unsigned int i, s, iarr, curr_s = dev->last_read;
	struct array_config* restrict ac = dev->arrconf;
	char* restrict ringbuffer = dev->buffer;
	char* restrict buffout[dev->narr];
	va_list ap;
	int error;

	va_start(ap, ns);
	for (i=0; i<dev->narr; i++) 
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
		for (i=0; i<dev->narr; i++)
			buffout[i] += dev->strides[i];
	}

	// Update the reading status
	pthread_mutex_lock(&(dev->synclock));
	dev->ns_read += ns;
	pthread_mutex_unlock(&(dev->synclock));

	dev->last_read = curr_s;
	return ns;
}


API_EXPORTED
ssize_t egd_get_available(struct eegdev* dev)
{
	int ns, error;

	if (!dev)
		return reterrno(EINVAL);

	pthread_mutex_lock(&(dev->synclock));
	ns = dev->ns_written - dev->ns_read;
	error = dev->error;
	pthread_mutex_unlock(&(dev->synclock));

	if (!ns && error)
		return reterrno(error);

	return ns;
}


API_EXPORTED
int egd_start(struct eegdev* dev)
{
	int acquiring;

	if (!dev)
		return reterrno(EINVAL);

	pthread_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	pthread_mutex_unlock(&(dev->synclock));
	if (acquiring)
		return reterrno(EPERM);
	
	pthread_mutex_lock(&(dev->synclock));
	dev->ns_read = dev->ns_written = 0;
	dev->ops.start_acq(dev);

	dev->acq_order = EGD_ORDER_START;
	dev->acquiring = 1;
	pthread_mutex_unlock(&(dev->synclock));

	return 0;
}


API_EXPORTED
int egd_stop(struct eegdev* dev)
{
	int acquiring;

	if (!dev)
		return reterrno(EINVAL);

	pthread_mutex_lock(&(dev->synclock));
	acquiring = dev->acquiring;
	pthread_mutex_unlock(&(dev->synclock));
	if (!acquiring)
		return reterrno(EPERM);

	pthread_mutex_lock(&(dev->synclock));
	dev->acq_order = EGD_ORDER_STOP;
	pthread_mutex_unlock(&(dev->synclock));

	dev->ops.stop_acq(dev);
	return 0;
}


static char eegdev_string[] = PACKAGE_STRING " (builtin: -"
#if XDF_SUPPORT
"eegfile-"
#endif
#if ACT2_SUPPORT
"biosemi-"
#endif
#if GTEC_SUPPORT
"gtec-"
#endif
#if NSKY_SUPPORT
"neurosky-"
#endif
")";

API_EXPORTED
const char* egd_get_string(void)
{
	return eegdev_string;	
}
