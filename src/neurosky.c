#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>


#include "eegdev-types.h"
#include "eegdev-common.h"



struct nsky_eegdev {
	struct eegdev dev;
	pthread_t thread_id;
	FILE *rfcomm;
	unsigned int runacq; 
};


#define get_nsky(dev_p) \
	((struct nsky_eegdev*)(((char*)(dev_p))-offsetof(struct nsky_eegdev, dev)))



// neurosky methods declaration
static int nsky_close_device(struct eegdev* dev);
static int nsky_noaction(struct eegdev* dev);
static int nsky_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);


static const struct eegdev_operations nsky_ops = {
	.close_device = nsky_close_device,
	.start_acq = nsky_noaction,
	.stop_acq = nsky_noaction,
	.set_channel_groups = nsky_set_channel_groups,
};

/******************************************************************
 *                       NSKY internals                     	  *
 ******************************************************************/
#define CODE	0xB0
#define EXCODE 	0x55
#define SYNC 	0xAA
#define NCH 	7
	
static const union gval nsky_scales[EGD_NUM_DTYPE] = {
	[EGD_INT32] = {.i32val = 1},
	[EGD_FLOAT] = {.fval = 3.0f / (511.0f*2000.0f)},	// in uV
	[EGD_DOUBLE] = {.dval = 3.0 / (511.0*2000.0)}		// in uV
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
	int runacq, ns;
	int32_t data[NCH];
	size_t samlen = sizeof(data);
	FILE* stream = nskydev->rfcomm;
	uint8_t c, pLength;

	while (1) {
		pthread_mutex_lock(&(nskydev->dev.synclock));
		runacq = nskydev->runacq;
		pthread_mutex_unlock(&(nskydev->dev.synclock));
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


static int nsky_set_capability(struct nsky_eegdev* nskydev)
{

	nskydev->dev.cap.sampling_freq = 128;
	nskydev->dev.cap.eeg_nmax = NCH;
	nskydev->dev.cap.sensor_nmax = 0;
	nskydev->dev.cap.trigger_nmax = 0;

	nskydev->dev.in_samlen = NCH*sizeof(int32_t);

	return 0;
}

/******************************************************************
 *               NSKY methods implementation                	  *
 ******************************************************************/
API_EXPORTED
struct eegdev* egd_open_neurosky(const char *path)
{
	struct nsky_eegdev* nskydev = NULL;
	FILE *stream;	
	int ret;

	if(!(nskydev = malloc(sizeof(*nskydev))))
		return NULL;
	
	stream = fopen(path,"r");
	if(!stream)
		goto error;

	if (egd_init_eegdev(&(nskydev->dev), &nsky_ops))
		goto error;

	nsky_set_capability(nskydev);
	
	nskydev->runacq = 1;
	nskydev->rfcomm = stream;

	if ((ret = pthread_create(&(nskydev->thread_id), NULL, 
	                           nsky_read_fn, nskydev)))
		goto error;
	
	return &(nskydev->dev);

error:
	free(nskydev);
	return NULL;
}


static
int nsky_close_device(struct eegdev* dev)
{
	struct nsky_eegdev* nskydev = get_nsky(dev);


	pthread_mutex_lock(&(nskydev->dev.synclock));
	nskydev->runacq = 0;
	pthread_mutex_unlock(&(nskydev->dev.synclock));

	pthread_join(nskydev->thread_id, NULL);
	
	egd_destroy_eegdev(&(nskydev->dev));
	fclose(nskydev->rfcomm);
	free(nskydev);
	
	return 0;
}


static
int nsky_noaction(struct eegdev* dev)
{
	(void)dev;
	return 0;
}


static
int nsky_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	unsigned int i;
	struct selected_channels* selch = dev->selch;
	
	for (i=0; i<ngrp; i++) {
		// Set parameters of (eeg -> ringbuffer)
		selch[i].in_offset = grp[i].index*sizeof(int32_t);
		selch[i].len = grp[i].nch*sizeof(int32_t);
		selch[i].cast_fn = egd_get_cast_fn(EGD_INT32, 
		                                   grp[i].datatype, 1);

		selch[i].sc = nsky_scales[grp[i].datatype];
	}
		
	return 0;
}







