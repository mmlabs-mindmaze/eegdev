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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#include <eegdev-pluginapi.h>

struct nsky_eegdev {
	struct devmodule dev;
	pthread_t thread_id;
	FILE *rfcomm;
	pthread_mutex_t acqlock;
	unsigned int runacq; 
	char bt_addr[24];
};

#define get_nsky(dev_p) ((struct nsky_eegdev*)(dev_p))

#define DEFAULT_NSKYDEV	"10:00:E8:AD:B1:EE"

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
static const char nskytransducer[] = "Dry electrode";
	
static const union gval nsky_scales[EGD_NUM_DTYPE] = {
	[EGD_INT32] = {.valint32_t = 1},
	[EGD_FLOAT] = {.valfloat = 3.0f / (511.0f*2000.0f)},	// in uV
	[EGD_DOUBLE] = {.valdouble = 3.0 / (511.0*2000.0)}	// in uV
};
static const int nsky_provided_stypes[] = {EGD_EEG};

static const struct egdi_optname nsky_options[] = {
	{.name = "baddr", .defvalue = DEFAULT_NSKYDEV},
	{.name = NULL}
};


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
	const struct core_interface* restrict ci = &nskydev->dev.ci;
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
		if (ci->update_ringbuffer(&(nskydev->dev), data, samlen*ns))
			break;
	}
	
	return NULL;
error:
	ci->report_error(&(nskydev->dev), EIO);
	return NULL;
}


static
int nsky_set_capability(struct nsky_eegdev* nskydev, const char* baddr)
{
	struct systemcap cap = {
		.sampling_freq = 128, 
		.type_nch = {[EGD_EEG] = NCH},
		.device_type = "Neurosky",
		.device_id = baddr
	};
	struct devmodule* dev = &nskydev->dev;

	dev->ci.set_cap(dev, &cap);
	dev->ci.set_input_samlen(dev, NCH*sizeof(int32_t));
	return 0;
}


static
int connect_bluetooth_dev(const char* baddr)
{
	struct sockaddr_rc addr;
	int s;

	// allocate a socket
	s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	fcntl(s, F_SETFD, fcntl(s, F_GETFD)|FD_CLOEXEC);

	// set the connection parameters (who to connect to)
	memset(&addr, 0, sizeof(addr));
	addr.rc_family = AF_BLUETOOTH;
	addr.rc_channel = (uint8_t) 1;
	str2ba( baddr, &addr.rc_bdaddr );

	// connect to server
	if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(s);
		return -1;
	}

	return s;
}

/******************************************************************
 *               NSKY methods implementation                	  *
 ******************************************************************/
static
int nsky_open_device(struct devmodule* dev, const char* optv[])
{
	FILE *stream;	
	int ret, fd;
	struct nsky_eegdev* nskydev = get_nsky(dev);
	const char* baddr = optv[0];

	// Open the device with CLOEXEC flag as soon as possible
	// (if possible)
	if ((fd = connect_bluetooth_dev(baddr)) < 0)
		return -1;

	stream = fdopen(fd,"r");
	if (!stream) {
		if (errno == ENOENT)
			errno = ENODEV;
		goto error;
	}

	nsky_set_capability(nskydev, baddr);
	
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
int nsky_close_device(struct devmodule* dev)
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
int nsky_set_channel_groups(struct devmodule* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	unsigned int i;
	struct selected_channels* selch;
	
	if (!(selch = dev->ci.alloc_input_groups(dev, ngrp)))
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


static void nsky_fill_chinfo(const struct devmodule* dev, int stype,
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
	info->transducer = nskytransducer;
}


API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct nsky_eegdev),
	.open_device = 		nsky_open_device,
	.close_device = 	nsky_close_device,
	.set_channel_groups = 	nsky_set_channel_groups,
	.fill_chinfo = 		nsky_fill_chinfo,
	.supported_opts =	nsky_options
};

