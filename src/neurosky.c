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

static 
unsigned int parse_payload(uint8_t *payload, unsigned char pLength,
                           int32_t *values)
{
	unsigned char bp = 0;
	unsigned char code, vlength, extCodeLevel;
	uint8_t datH, datL;
	unsigned int i,ns=0;
	
	//Parce the extended Code
	while (bp < pLength) {
		// Identifying extended code level
		extCodeLevel=0;
		while(payload[bp] == EXCODE){
			extCodeLevel++;
			bp++;
		}

		// Identifying the DataRow type
		code = payload[bp++];
		vlength=payload[bp++];
		if (code < 0x80)
			continue;

		// decode EEG values
		for (i=0; i<vlength/2; i++) {
			datH = payload[bp++];
			datL = payload[bp++];
			if(datH & 0x10)
				datL=0x02;
	
			datH &= 0x03;
			values[i+ns*NCH] = datH*256 + datL;
		}
		ns++;
		bp += vlength;
	}	

	return ns;
}


static int sync_data(FILE *stream, int32_t *data)
{
	unsigned char c;
	unsigned int check_sum, ready_flag = 0;
	unsigned char pLength;
	unsigned char payload[256];
	unsigned int i;
	int retval = -1;
	
	while(!ready_flag){
		// Read SYNC Bytes
		if (fread(&c,1,1,stream) < 1)
			break;
		if (c != SYNC)
			continue;
		if (fread(&c,1,1,stream) < 1)
			break;
		if (c != SYNC)
			continue;

		//Read Plength
		while (1) {
			if (fread(&pLength,1,1,stream) < 1)
				break;
			if (pLength != SYNC)
				break;
		}
		if (pLength > 0xA9)
			continue;

		//Read Payload
		if(fread(payload,1,pLength,stream) < pLength)
			break;
	
		// Calculate Check Sum
		check_sum=0;
		for(i=0;i<pLength;i++)
			check_sum+=payload[i];
		
		check_sum &= 0xFF;
		check_sum = ~check_sum & 0xFF;
	
		// Parse Check sum from data
		if (fread(&c,1,1,stream) < 1)
			break;

		//Verify Check sum
		if((int)(c) != check_sum)
			continue;
	
		retval = parse_payload(payload,pLength,data);
		ready_flag=1;
	}

	return retval;
}


static void* nsky_read_fn(void* arg)
{
	struct nsky_eegdev* nskydev = arg;
	int runacq,ns;
	int32_t data[NCH];
	size_t samlen = sizeof(data);

	while (1) {
		pthread_mutex_lock(&(nskydev->dev.synclock));
		runacq = nskydev->runacq;
		pthread_mutex_unlock(&(nskydev->dev.synclock));
		if (!runacq)
			break;

		// Update the eegdev structure with the new data
		ns = sync_data(nskydev->rfcomm, data);
		if (ns < 0){
			egd_report_error(&(nskydev->dev), EIO);
			break;
		}
				
		if (egd_update_ringbuffer(&(nskydev->dev), data, samlen*ns))
			break;
	}
	
	return NULL;
}


static int nsky_set_capability(struct nsky_eegdev* nskydev)
{

	nskydev->dev.cap.sampling_freq = 127;
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

	if ((ret = pthread_create(&(nskydev->thread_id), NULL,nsky_read_fn, nskydev)))
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

		/* TODO: Set the correct scale */
		selch[i].sc.i32val = 1;
	}
		
	return 0;
}







