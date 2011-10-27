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
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>

#include <eegdev-common.h>

struct nsky_eegdev {
	struct eegdev dev;
	pthread_t thread_id;
	FILE *rfcomm;
	pthread_mutex_t acqlock;
	unsigned int runacq; 
};

#define get_nsky(dev_p) ((struct nsky_eegdev*)(dev_p))

#define DEFAULT_NSKYDEV	"/dev/rfcomm0"

/******************************************************************
 *                       NSKY internals                     	  *
 ******************************************************************/
#define CODE	0xB0
#define EXCODE 	0x55
#define SYNC 	0xAA
#define NCH 	7

static const char nskylabel[8][NCH] = {
	"EEG1", "EEG2", "EEG3", "EEG4", "EEG5", "EEG6", "EEG7"
};
static const char nskyunit[] = "uV";
static const char nskytransducter[] = "Dry electrode";
	
static const union gval nsky_scales[EGD_NUM_DTYPE] = {
	[EGD_INT32] = {.valint32_t = 1},
	[EGD_FLOAT] = {.valfloat = 3.0f / (511.0f*2000.0f)},	// in uV
	[EGD_DOUBLE] = {.valdouble = 3.0 / (511.0*2000.0)}	// in uV
};
static const int nsky_provided_stypes[] = {EGD_EEG};

static 
unsigned int parse_payload(uint8_t *payload, unsigned int pLength,
                           int32_t *values)
{
	unsigned char bp = 0;
	unsigned char code, vlength, extCodeLevel;
	uint8_t datH, datL;
	unsigned int i,ns=0;
	
	//Parse the extended Code
	while (bp < pLength) {
		// Identifying extended code level
		extCodeLevel=0;
		while(payload[bp] == EXCODE){
			extCodeLevel++;
			bp++;
		}

		// Identifying the DataRow type
		code = payload[bp++];
		vlength = payload[bp++];
		if (code < 0x80)
			continue;

		// decode EEG values
		for (i=0; i<vlength/2; i++) {
			datH = payload[bp++];
			datL = payload[bp++];
			if(datH & 0x10)
				datL=0x02;
	
			datH &= 0x03;
			values[i+ns*NCH] = (datH*256 + datL) - 512;
		}
		ns++;
		bp += vlength;
	}	

	return ns;
}


static
int read_payload(FILE* stream, unsigned int len, int32_t* data)
{
	unsigned int i;
	uint8_t payload[192];
	unsigned int checksum = 0;

	//Read Payload + checksum
	if (fread(payload, len+1, 1, stream) < 1)
		return -1;
	
	// Calculate Check Sum
	for (i=0; i<len; i++)
		checksum += payload[i];
	checksum &= 0xFF;
	checksum = ~checksum & 0xFF;
	
	// Verify Check sum (which is the last byte read)
	// and parse if correct
	if ((unsigned int)(payload[len]) == checksum)
		return parse_payload(payload, len, data);
	
	return 0;
}


static void* nsky_read_fn(void* arg)
{
	struct nsky_eegdev* nskydev = arg;
	int runacq, ns;
	int32_t data[NCH];
	size_t samlen = sizeof(data);
	FILE* stream = nskydev->rfcomm;
	uint8_t c, pLength;

	while (1) {
		pthread_mutex_lock(&(nskydev->acqlock));
		runacq = nskydev->runacq;
		pthread_mutex_unlock(&(nskydev->acqlock));
		if (!runacq)
			break;

		// Read SYNC Bytes
		if (fread(&c, 1, 1, stream) < 1)
			goto error;
		if (c != SYNC)
			continue;
		if (fread(&c, 1, 1, stream) < 1)
			goto error;
		if (c != SYNC)
			continue;

		//Read Plength
		do {
			if (fread(&pLength, 1, 1, stream) < 1)
				goto error;
		} while (pLength == SYNC);
		if (pLength > 0xA9)
			continue;

		ns = read_payload(stream, pLength, data);
		if (ns < 0)
			goto error;
		if (ns == 0)
			continue;

		// Update the eegdev structure with the new data
		if (egd_update_ringbuffer(&(nskydev->dev), data, samlen*ns))
			break;
	}
	
	return NULL;
error:
	egd_report_error(&(nskydev->dev), EIO);
	return NULL;
}


static
int nsky_set_capability(struct nsky_eegdev* nskydev)
{

	nskydev->dev.cap.sampling_freq = 128;
	nskydev->dev.cap.type_nch[EGD_EEG] = NCH;
	nskydev->dev.cap.type_nch[EGD_SENSOR] = 0;
	nskydev->dev.cap.type_nch[EGD_TRIGGER] = 0;

	egd_set_input_samlen(&(nskydev->dev), NCH*sizeof(int32_t));

	return 0;
}

/******************************************************************
 *               NSKY methods implementation                	  *
 ******************************************************************/
static
int nsky_open_device(struct eegdev* dev, const char* optv[])
{
	FILE *stream;	
	int ret, fd;
	struct nsky_eegdev* nskydev = get_nsky(dev);
	const char* devpath = egd_getopt("path", DEFAULT_NSKYDEV, optv);

	// Open the device with CLOEXEC flag as soon as possible
	// (if possible)
#if HAVE_DECL_O_CLOEXEC
	fd = open(devpath, O_RDONLY|O_CLOEXEC);
#else
	fd = open(devpath, O_RDONLY);
	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD)|FD_CLOEXEC);
#if HAVE_DECL_FD_CLOEXEC
#endif
#endif
	stream = fdopen(fd,"r");
	if (!stream) {
		if (errno == ENOENT)
			errno = ENODEV;
		goto error;
	}

	nsky_set_capability(nskydev);
	
	pthread_mutex_init(&(nskydev->acqlock), NULL);
	nskydev->runacq = 1;
	nskydev->rfcomm = stream;

	if ((ret = pthread_create(&(nskydev->thread_id), NULL, 
	                           nsky_read_fn, nskydev)))
		goto error;
	
	return 0;

error:
	return -1;
}


static
int nsky_close_device(struct eegdev* dev)
{
	struct nsky_eegdev* nskydev = get_nsky(dev);


	pthread_mutex_lock(&(nskydev->acqlock));
	nskydev->runacq = 0;
	pthread_mutex_unlock(&(nskydev->acqlock));

	pthread_join(nskydev->thread_id, NULL);
	pthread_mutex_destroy(&(nskydev->acqlock));
	
	fclose(nskydev->rfcomm);
	
	return 0;
}


static
int nsky_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	unsigned int i;
	struct selected_channels* selch;
	
	if (!(selch = egd_alloc_input_groups(dev, ngrp)))
		return -1;

	for (i=0; i<ngrp; i++) {
		// Set parameters of (eeg -> ringbuffer)
		selch[i].in_offset = grp[i].index*sizeof(int32_t);
		selch[i].inlen = grp[i].nch*sizeof(int32_t);
		selch[i].bsc = 1;
		selch[i].typein = EGD_INT32;
		selch[i].sc = nsky_scales[grp[i].datatype];
		selch[i].typeout = grp[i].datatype;
		selch[i].iarray = grp[i].iarray;
		selch[i].arr_offset = grp[i].arr_offset;
	}
		
	return 0;
}


static void nsky_fill_chinfo(const struct eegdev* dev, int stype,
	                     unsigned int ich, struct egd_chinfo* info)
{
	(void)dev;
	(void)stype;

	info->isint = 0;
	info->dtype = EGD_DOUBLE;
	info->min.valdouble = -512.0 * nsky_scales[EGD_DOUBLE].valdouble;
	info->max.valdouble = 511.0 * nsky_scales[EGD_DOUBLE].valdouble;
	info->label = nskylabel[ich];
	info->unit = nskyunit;
	info->transducter = nskytransducter;
}


API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct nsky_eegdev),
	.open_device = 		nsky_open_device,
	.close_device = 	nsky_close_device,
	.set_channel_groups = 	nsky_set_channel_groups,
	.fill_chinfo = 		nsky_fill_chinfo
};

