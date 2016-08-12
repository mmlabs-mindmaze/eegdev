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
#include "device-helper.h"
struct bbt_eegdev {
	struct devmodule dev;
	int fs;
	unsigned int nch;
	struct egdich* chmap;
	pthread_t thid;
	pthread_mutex_t mtx;
	FILE *rfcomm;
	unsigned int runacq; 
	char bt_addr[18];
};

#define DEFAULT_ADDRESS	"00:06:66:A0:4D:B7"
#define NUM_CHANNELS_LOOP	23
#define SAMPLING_RATE	256
#define BLOCK_SIZE 1

#define BYTES_PER_INT 4
#define BYTES_PER_EEG_EMG_SAMPLE 3
#define BYTES_PER_DIG_BAT_EMPTY 1

#define EEG_CHANNELS 16
#define EMG_CHANNELS 6
#define DIGITAL_CHANNELS 3
#define BATERY_CHANNELS 3
#define EMPTY_CHANNELS 0

#define get_bbt(dev_p) ((struct bbt_eegdev*)(dev_p))


/******************************************************************
 *                       BBT internals                     	  *
 ******************************************************************/


static const char bbtlabeleeg[EEG_CHANNELS][8] = {
	"Fz", "FC3", "FCz", "FC4", "C3", "Cz", "C4",
	"CP3", "CPz", "CP4", "P5 ", "POz", "P6 ", "O1 ",
	"O2 ", "EEG_Ax" 
};
static const char bbtlabelemg[EMG_CHANNELS][8] = {
	"EXG1", "EXG2", "EXG3", 
	"EXG4", "EXG5", "EXG6"
};
static const char bbtlabeltri[8] = "Trigger";

static const char bbtunit[] = "uV";
static const char bbttransducter[] = "Active electrode";
static const char bbtunit_trigger[] = "Boolean";
static const char bbttransducter_trigger[] = "Triggers and Status";
static const char bbtprefiltering[] = "None";
	
static const union gval bbt_scales[EGD_NUM_DTYPE] = {
	[EGD_INT32] = {.valint32_t = 1},
	[EGD_FLOAT] = {.valfloat = 1.0f},	// in uV
	[EGD_DOUBLE] = {.valdouble = 1.0f}	// in uV
};

enum {OPT_ADDRESS, NUMOPT};
static const struct egdi_optname bbt_options[] = {
	[OPT_ADDRESS] = {.name = "address", .defvalue = DEFAULT_ADDRESS},
	[NUMOPT] = {.name = NULL}
};


static
void parse_bbt_options(const char* optv[], struct devmodule* dev)
{
    struct bbt_eegdev* tdev = get_bbt(dev);
    strcpy(tdev->bt_addr,optv[OPT_ADDRESS]); 
}

static
void* bbt_read_fn(void *data)
{
	struct bbt_eegdev* tdev = data;
	const struct core_interface* restrict ci = &tdev->dev.ci;

	int length_data_raw = BLOCK_SIZE *
		(EEG_CHANNELS*BYTES_PER_EEG_EMG_SAMPLE + EMG_CHANNELS*BYTES_PER_EEG_EMG_SAMPLE +
        DIGITAL_CHANNELS*BYTES_PER_DIG_BAT_EMPTY + BATERY_CHANNELS*BYTES_PER_DIG_BAT_EMPTY +
        EMPTY_CHANNELS*BYTES_PER_DIG_BAT_EMPTY);
        
	char* data_raw = (char*)malloc(length_data_raw);	
	
	char* _mpEegEmgBytes =  (char*)  malloc( (EEG_CHANNELS + EMG_CHANNELS) * BLOCK_SIZE * BYTES_PER_INT);
    char* _mpDigitalBytes = (char*)  malloc( DIGITAL_CHANNELS * BLOCK_SIZE * 1);
    char* _mpBateryBytes =  (char*)  malloc( BATERY_CHANNELS * BLOCK_SIZE * 1);

	unsigned int* auxc = (unsigned int*) (_mpEegEmgBytes);
	char* auxDig = _mpDigitalBytes;
	char* auxBat = _mpBateryBytes;

	unsigned int extensorSigno = 0;
	int valorExtendido = 0;
	int mascara = 0x00800000;
	int hexaExtender = 0xFF000000;

	int length_data_float = (NUM_CHANNELS_LOOP) * BLOCK_SIZE * sizeof(float); 
	float* dataDst = malloc(length_data_float);
	float* auxdst;

	int bytes_read;
	int lastIndex = 0;


    for(int i = 0; i < (EEG_CHANNELS + EMG_CHANNELS) * BLOCK_SIZE * BYTES_PER_INT; i++)
	{
        _mpEegEmgBytes[i] = 0;
	}

    for(int i = 0; i < DIGITAL_CHANNELS * BLOCK_SIZE * 1; i++)
    {
        _mpDigitalBytes[i] = 0;
    } 

	for(int i = 0; i < BATERY_CHANNELS * BLOCK_SIZE * 1; i++)
    {
        _mpBateryBytes[i] = 0;
    }
	
	fseek(tdev->rfcomm, 0, SEEK_END);

	
	while(1){
		
		// Read the last part. If missed data, bad luck :(
		//fseek(tdev->rfcomm, -length_data_raw, SEEK_END);
		bytes_read = fread(data_raw, sizeof(char), length_data_raw, tdev->rfcomm);
		
		lastIndex = 0;
        for (int j = 0; j < BLOCK_SIZE; j++)
        {
            //Read EEG and EMG
            int indexEegEmg = (EEG_CHANNELS + EMG_CHANNELS)*4*j;
            for (int i = 0; i < EEG_CHANNELS*BYTES_PER_EEG_EMG_SAMPLE + EMG_CHANNELS*BYTES_PER_EEG_EMG_SAMPLE; i++)
            {
                _mpEegEmgBytes[indexEegEmg] = data_raw[lastIndex + i + 2];

                _mpEegEmgBytes[indexEegEmg + 1] = data_raw[lastIndex + i + 1];

                _mpEegEmgBytes[indexEegEmg + 2] = data_raw[lastIndex + i];
                i+=2;
                indexEegEmg += 4;
                
            }

            lastIndex += EEG_CHANNELS*BYTES_PER_EEG_EMG_SAMPLE + EMG_CHANNELS*BYTES_PER_EEG_EMG_SAMPLE;

            //Read Digital
            int indexDigital = DIGITAL_CHANNELS*1*j;
            for (int i = 0; i < DIGITAL_CHANNELS*BYTES_PER_DIG_BAT_EMPTY; i++)
            {
                _mpDigitalBytes[indexDigital + i] = data_raw[lastIndex + i];
            }

            lastIndex += DIGITAL_CHANNELS*BYTES_PER_DIG_BAT_EMPTY;

            //Read Batery
            int indexBat = BATERY_CHANNELS*1*j;
            for (int i = 0; i < BATERY_CHANNELS*BYTES_PER_DIG_BAT_EMPTY; i++)
            {
                _mpBateryBytes[indexBat + i] = data_raw[lastIndex + i];             
            }

            lastIndex += BATERY_CHANNELS*BYTES_PER_DIG_BAT_EMPTY;

            //Skip Empty channels
            lastIndex += EMPTY_CHANNELS*BYTES_PER_DIG_BAT_EMPTY;
        }


		auxc = (unsigned int*)(_mpEegEmgBytes);
		auxDig = _mpDigitalBytes;
		auxBat = _mpBateryBytes;
		auxdst = dataDst;


		auxdst = dataDst;

		for(int k = 0; k < BLOCK_SIZE; k++)
		{
			//Sign extend and conversion of eeg data
			for(int l = 0; l < EEG_CHANNELS; l++)
			{

				extensorSigno = *auxc;
				valorExtendido = *auxc;
				extensorSigno = extensorSigno & mascara;
				if (extensorSigno != 0)
					valorExtendido = valorExtendido | hexaExtender;

                		*auxdst = (((2.5/25.7)/0x780000)*((double)valorExtendido))*10e5;
		                auxdst++;
				
				auxc++;
			}
			//Sign extend and conversion of emg data
			for(int l = 0; l < EMG_CHANNELS; l++)
			{
				extensorSigno = *auxc;
				valorExtendido = *auxc;
				extensorSigno = extensorSigno & mascara;
				if (extensorSigno != 0)
					valorExtendido = valorExtendido | hexaExtender;

                		*auxdst = (((2.5/5.94)/0x780000)*((double)valorExtendido))*10e5;
                
				auxc++;
                		auxdst++;
			}
			
			//Fill trigger channel
			*auxdst = (double) ((auxDig[0] + 2 * auxDig[1] + 4 * auxDig[2]) / 0x7F);
			auxDig=auxDig+3;
			
			auxdst++;
        }//end for k

        
		ci->update_ringbuffer(&(tdev->dev), dataDst, length_data_float);

	}
	// We can reach here only if there was an error previously
	ci->report_error(&tdev->dev, errno);
	return NULL;
}


static
int bbt_set_capability(struct bbt_eegdev* bbtdev)
{
	struct bbt_eegdev* tdev = get_bbt(bbtdev);
	tdev->chmap = malloc(NUM_CHANNELS_LOOP*sizeof(*tdev->chmap));
	
	//EEG
	for (int i=0; i<EEG_CHANNELS; i++) {
		tdev->chmap[i].dtype = EGD_FLOAT;
		tdev->chmap[i].stype = EGD_EEG;
	}

	//EMG
	for (int i=EEG_CHANNELS; i<EEG_CHANNELS+EMG_CHANNELS; i++) {
		tdev->chmap[i].dtype = EGD_FLOAT;	
		tdev->chmap[i].stype = EGD_SENSOR;
	}

	//TRIGGER
	tdev->chmap[22].dtype = EGD_FLOAT;	
	tdev->chmap[22].stype = EGD_TRIGGER;

	struct systemcap cap = {.type_nch = {0}};

	for (int i=0; i<NUM_CHANNELS_LOOP; i++)
		cap.type_nch[tdev->chmap[i].stype]++;

	// Fill the capabilities metadata
	cap.sampling_freq = SAMPLING_RATE;
	cap.device_type = "BBT";
	cap.device_id = tdev->bt_addr;

	struct devmodule* dev = &tdev->dev;

	dev->ci.set_cap(dev, &cap);
	dev->ci.set_input_samlen(dev, NUM_CHANNELS_LOOP*sizeof(float));
	
	return 0;
}




static
int init_data_com(struct devmodule* dev, const char* optv[])
{
	parse_bbt_options(optv, dev);
	struct bbt_eegdev* tdev = get_bbt(dev);

	struct timespec tim;
	tim.tv_sec = 0;
	tim.tv_nsec = 500000000;
	
	// Set capabilities
	bbt_set_capability(tdev);

	struct sockaddr_rc addr = { 0 };
    	int s, status;

	// allocate a socket
	s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	fcntl(s, F_SETFD, fcntl(s, F_GETFD)|FD_CLOEXEC);

	// set the connection parameters (who to connect to)
	memset(&addr, 0, sizeof(addr));
	addr.rc_family = AF_BLUETOOTH;
	addr.rc_channel = (uint8_t) 1;
	str2ba( tdev->bt_addr, &addr.rc_bdaddr );
	
	// connect to server
	if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "Couldn't connect to the device\n");		
		close(s);
		return -1;
	}
	tdev->rfcomm = fdopen(s,"r+");
	if (!tdev->rfcomm) {
		fprintf(stderr, "Couldn't open the device\n");
		return -1;
	} else {
		fprintf(stdout, "Connected to BBT cap\n");
	}

	if (fwrite("s", sizeof(char), 1, tdev->rfcomm) < 0) {
		fprintf(stderr, "Couldn't write through bluetooth\n");
		return -1;
	}
	
	// Give it some time
	nanosleep(&tim,NULL);

	int threadretval = pthread_create(&tdev->thid, NULL, bbt_read_fn, tdev); 

  	if (threadretval < 0) {
		fclose(tdev->rfcomm);
		tdev->rfcomm = NULL;
		return -1;
	}
	tdev->runacq = 1;
	return 0;
}
/******************************************************************
 *               BBT methods implementation                	  *
 ******************************************************************/

static
int bbt_close_device(struct devmodule* dev)
{
	printf("Closing bbt device\n");
	struct bbt_eegdev* tdev = get_bbt(dev);
	tdev->runacq = 0;


	if (fwrite("p", sizeof(char), 1, tdev->rfcomm) < 0) {
		fprintf(stderr, "Couldn't write stop message through bluetooth\n");
		return -1;
	}

	// Destroy data connection
	if (tdev->rfcomm >= 0) {
		pthread_cancel(tdev->thid);
		pthread_join(tdev->thid, NULL);
		fclose(tdev->rfcomm);
	}
	printf("Closed successfully\n");

	// Free channels metadata
	free(tdev->chmap);
	return 0;
}
static
int bbt_open_device(struct devmodule* dev, const char* optv[])
{
	if (init_data_com(dev, optv)) 
	{
		bbt_close_device(dev);
		return -1;
	}

	return 0;

}

static
int bbt_set_channel_groups(struct devmodule* dev, unsigned int ngrp,
					const struct grpconf* grp)
{

	struct bbt_eegdev* tdev = get_bbt(dev);
	struct selected_channels* selch;
	int i, nsel = 0;

	nsel = egdi_split_alloc_chgroups(dev, tdev->chmap, ngrp, grp, &selch);
	for (i=0; i<nsel; i++)
		selch[i].bsc = 0;

	return (nsel < 0) ? -1 : 0;

}


static void bbt_fill_chinfo(const struct devmodule* dev, int stype,
	                     unsigned int ich, struct egd_chinfo* info)
{
	struct bbt_eegdev* tdev = get_bbt(dev);
	if (stype == EGD_EEG) {
		info->isint = 0;
		info->dtype = EGD_FLOAT;
		info->min.valfloat = -16384.0;
		info->max.valfloat = +16384.0;
		info->unit = bbtunit;
		info->transducter = bbttransducter;
		info->prefiltering = bbtprefiltering;
		info->label = bbtlabeleeg[ich];
	} else if (stype == EGD_SENSOR) {
		info->isint = 0;
		info->dtype = EGD_FLOAT;
		info->min.valfloat = -16384.0;
		info->max.valfloat = +16384.0;
		info->unit = bbtunit;
		info->transducter = bbttransducter;
		info->prefiltering = bbtprefiltering;
		info->label = bbtlabelemg[ich];
	} else {
		info->isint = 1;
		info->dtype = EGD_INT32;
		info->min.valint32_t = -8388608;
		info->max.valint32_t = 8388607;
		info->unit = bbtunit_trigger;
		info->transducter = bbttransducter;
		info->prefiltering = bbtprefiltering;
		info->label = bbtlabeltri;
	}
}

API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct bbt_eegdev),
	.open_device = 		bbt_open_device,
	.close_device = 	bbt_close_device,
	.set_channel_groups = 	bbt_set_channel_groups,
	.fill_chinfo = 		bbt_fill_chinfo,
	.supported_opts =	bbt_options
};

