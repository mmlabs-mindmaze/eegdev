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
#include <sys/time.h>
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

#include <eegdev-pluginapi.h>
#include <gtecnalang.h>
#include "device-helper.h"

struct gtecnet_eegdev {
    struct devmodule dev;
    struct egdich* chmap;

    pthread_t thid;
    pthread_mutex_t mtx;

    socketd_t sockcmd;
    socketd_t socklisten;
    socketd_t sockdata;

    DevConfig* devconf;

    void* Transceiver;

    float* databuffer;

    char SessionID[5];
    char ConnectedDevices[10000];   //Allocate enough memory
    char DeviceType[20];
    char DeviceName[20];
    char ScanInfo[10000];           //Allocate enough memory
    char* host_ip;
    char* local_ip;
    	
    int NchannelsALL;
    int NchannelsEEG;
    int NchannelsEXG;
    int NchannelsTRIG;
    int DataPointsPerScan;
    int ScanCount;
    int SamplingRate;
    int ScansPerFrame;
    int host_port;
    int local_port;
    int notchFilter;
    int BPFilter;
    int usedefmap;

    unsigned int runacq;
};


#define NUMELEM(x)  (sizeof(x) / sizeof(x[0]))
#define get_gtecnet(dev_p) ((struct gtecnet_eegdev*)(dev_p))


//-------------------------------------
// Hardcoded definitions
//-------------------------------------

#define DEFAULT_HOST_IP "192.168.1.1"
#define DEFAULT_HOST_PORT "50223"

#define DEFAULT_LOCAL_IP "192.168.1.101"
#define DEFAULT_LOCAL_PORT "50220"

#define DEFAULT_SAMPLING_RATE "512"

#define DEFAULT_NOTCH "4"       // 512 Hz -> 48 - 52 Hz
#define DEFAULT_BANDPASS "32"   // 512 Hz -> 0.01 - 100 Hz

#define DEFAULT_USEDEFMAP "1"

/******************************************************************
 *                       g.NEEDaccess metadata                     	  *
 ******************************************************************/
static const char gtecnetlabelHIamp[81][10] = {
    "FP1","FPz","FP2","AF7","AF3","AF4","AF8","F7","F5","F3",
    "F1","Fz","F2","F4","F6","F8","FT7","FC5","FC3","FC1",
    "FCz","FC2","FC4","FC6","FT8","T7","C5","C3","C1","Cz",
    "C2","C4","C6","T8","TP7","CP5","CP3","CP1","CPz","CP2",
    "CP4","CP6","TP8","P7","P5","P3","P1","Pz","P2","P4",
    "P6","P8","PO7","PO3","POz","PO4","PO8","O1","Oz","O2",
    "F9","F10","REF1","REF2",
    "EXG1","EXG2","EXG3","EXG4","EXG5","EXG6", "EXG7","EXG8","EXG9","EXG10","EXG11","EXG12","EXG13","EXG14","EXG15","EXG16",
    "TRIG"
};

static const char gtecnetlabelUSBamp[17][10] = {
    "Fz","FC3","FC1","FCz","FC2","FC4","C3","C1","Cz","C2","C4","CP3","CP1","CPz","CP2","CP4","TRIG"
};

static const char gtecnetlabelNautilus[33][10] = {
    "FP1","FP2","AF3","AF4","F7","F3","Fz","F4","F8","FC5",
    "FC1","FC2","FC6","T7","C3","Cz","C4","T8","CP5","CP1",
    "CP2","CP6","P7","P3","Pz","P4","P8","PO7","PO3","PO4",
    "PO8","Oz", "TRIG"
};

static const char gtecnetlabelGen[81][10] = {
    "1","2","3","4","5","6","7","8","9","10",
    "11","12","13","14","15","16","17","18","19","20",
    "21","22","23","24","25","26","27","28","29","30",
    "31","32","33","34","35","36","37","38","39","40",
    "41","42","43","44","45","46","47","48","49","50",
    "51","52","53","54","55","56","57","58","59","60",
    "61","62","63","64","65","66","67","68","69","70",
    "71","72","73","74","75","76","77","78","79","80",
     "TRIG"
};

static const char gtecnetunit[] = "uV";
static const char gtecnettransducter[] = "Active electrode";
static const char gtecnetunit_trigger[] = "Boolean";
static const char gtecnettransducter_trigger[] = "Triggers and Status";
static const int gtecnet_provided_stypes[] = {EGD_EEG};

enum {OPT_HOSTIP, OPT_HOSTPORT, OPT_LOCALIP, OPT_LOCALPORT, OPT_SAMPLERATE, OPT_NOTCH, OPT_BANDPASS, OPT_USEDEFMAP, NUMOPT};
static const struct egdi_optname gtecnet_options[] = {
    [OPT_HOSTIP] = {.name = "hostIP", .defvalue = DEFAULT_HOST_IP},
    [OPT_HOSTPORT] = {.name = "hostport", .defvalue = DEFAULT_HOST_PORT},
    [OPT_LOCALIP] = {.name = "localIP", .defvalue = DEFAULT_LOCAL_IP},
    [OPT_LOCALPORT] = {.name = "localport", .defvalue = DEFAULT_LOCAL_PORT},
    [OPT_SAMPLERATE] = {.name = "samplerate", .defvalue = DEFAULT_SAMPLING_RATE},
    [OPT_NOTCH] = {.name = "notch", .defvalue = DEFAULT_NOTCH},
    [OPT_BANDPASS] = {.name = "bandpass", .defvalue = DEFAULT_BANDPASS},
    [OPT_USEDEFMAP] = {.name = "usedefmap", .defvalue = DEFAULT_USEDEFMAP},
    [NUMOPT] = {.name = NULL}
};


static
void parse_gtecnet_options(const char* optv[], struct devmodule* dev)
{
    struct gtecnet_eegdev* tdev = get_gtecnet(dev);

    tdev->host_ip = optv[OPT_HOSTIP];
    tdev->host_port = atoi(optv[OPT_HOSTPORT]);

    tdev->local_ip = optv[OPT_LOCALIP];
    tdev->local_port = atoi(optv[OPT_LOCALPORT]);

    tdev->SamplingRate = atoi(optv[OPT_SAMPLERATE]);
    tdev->BPFilter = atoi(optv[OPT_BANDPASS]);
    tdev->notchFilter = atoi(optv[OPT_NOTCH]);
    tdev->usedefmap = atoi(optv[OPT_USEDEFMAP]);
}


static
void* gtecnet_read_fn(void *data)
{
    struct gtecnet_eegdev* tdev = data;
    const struct core_interface* restrict ci = &tdev->dev.ci;

    tdev->databuffer = (float*)malloc(tdev->ScansPerFrame * tdev->DataPointsPerScan * sizeof(float));	
 
    //Disable DataReadyEventThreshold
    //int DataReadyEventThreshouldOutput = gtecnal_DataReadyEventThreshold(tdev->Transceiver, tdev->SessionID, tdev->ScansPerFrame);

    // Start acquisition
    int StartAcqOutput = gtecnal_StartAcquisition(tdev->Transceiver, tdev->SessionID);
    
    // Start streaming
    int StartStreaming = gtecnal_StartStreaming(tdev->Transceiver, tdev->SessionID);

    struct timeval start, end;
    while(true){
	if(tdev->runacq){
            gettimeofday(&start,NULL);	    
            int ScansRead = gtecnal_ReadDataFrame(tdev->Transceiver, tdev->databuffer, tdev->SessionID, &tdev->sockdata, tdev->ScansPerFrame, tdev->DataPointsPerScan);
            gettimeofday(&end,NULL);
	    float ElapsedTime = (float)1000*(end.tv_sec-start.tv_sec)+(end.tv_usec-start.tv_usec)/1000;
	    fprintf(stdout,"\n%f milliseconds for %d frames\n",ElapsedTime,ScansRead);
	    ci->update_ringbuffer(&(tdev->dev), tdev->databuffer, ScansRead * tdev->DataPointsPerScan * sizeof(float));
	}else{
	    // Exit reading thread
	    pthread_exit(NULL);
	}
    }
    // We can reach here only if there was an error previously
    ci->report_error(&tdev->dev, errno);
    return NULL;
}

static
int gtecnet_set_capability(struct gtecnet_eegdev* gtecnetdev)
{
	struct gtecnet_eegdev* tdev = get_gtecnet(gtecnetdev);
	tdev->chmap = malloc((tdev->NchannelsALL)*sizeof(*tdev->chmap));

	//EEG
	for (int i=0; i<tdev->NchannelsEEG; i++) {
		tdev->chmap[i].dtype = EGD_FLOAT;
		tdev->chmap[i].stype = EGD_EEG;
	}

        //EXG
        for (int i=tdev->NchannelsEEG; i<tdev->NchannelsEEG+tdev->NchannelsEXG; i++) {
		tdev->chmap[i].dtype = EGD_FLOAT;
		tdev->chmap[i].stype = EGD_SENSOR;
	}

        //TRIGGER
        tdev->chmap[tdev->NchannelsEEG+tdev->NchannelsEXG].dtype = EGD_FLOAT; //EGD_INT32?
        tdev->chmap[tdev->NchannelsEEG+tdev->NchannelsEXG].stype = EGD_TRIGGER;


	struct systemcap cap = {.type_nch = {0}};

	for (int i=0; i<tdev->NchannelsALL; i++)
	    cap.type_nch[tdev->chmap[i].stype]++;

	// Fill the capabilities metadata
	cap.sampling_freq = tdev->SamplingRate;
	cap.device_type = tdev->DeviceType;
	cap.device_id = tdev->DeviceName;

	struct devmodule* dev = &tdev->dev;

	dev->ci.set_cap(dev, &cap);
	dev->ci.set_input_samlen(dev, tdev->NchannelsALL * sizeof(float));

	return 0;
}

static
int gtecnet_close_device(struct devmodule* dev)
{
	struct gtecnet_eegdev* tdev = get_gtecnet(dev);

	// Try to stop requests for data
	tdev->runacq = 0;

	// Give some time the reading to finish
	sleep(1);

    	//Stop streaming
	int StopStreamOutput = gtecnal_StopStreaming(tdev->Transceiver, tdev->SessionID);

	// Stop acquisition
	int StopAcqOutput = gtecnal_StopAcquisition(tdev->Transceiver, tdev->SessionID);

	// Close acquisition session
	int CloseAcqSesOutput = gtecnal_CloseDAQSession(tdev->Transceiver, tdev->SessionID);

	// Disconnect from server
	int DisconnectServerOutput = gtecnal_Disconnect(tdev->Transceiver, tdev->SessionID);

	// Close connections
	gtecnal_CloseNetworkConnection(&tdev->socklisten);
	gtecnal_CloseNetworkConnection(&tdev->sockdata);
	gtecnal_CloseNetworkConnection(&tdev->sockcmd);

	// Delete the transceiver
	gtecnal_deleteTransceiver(tdev->Transceiver);

	//Free allocated memory
        free(tdev->devconf);
        free(tdev->databuffer);
	//Free channels metadata
	free(tdev->chmap);
	return 0;
}


static
int init_data_com(struct devmodule* dev, const char* optv[])
{

        parse_gtecnet_options(optv,dev);

        struct gtecnet_eegdev* tdev = get_gtecnet(dev);


	tdev->sockcmd = 0;
	tdev->socklisten = 0;
	tdev->sockdata = 0;

	tdev->ScanCount = 1;

	// Connect to server socket
	int EstablishNetworkConnectionOutput = gtecnal_EstablishNetworkConnection(&tdev->sockcmd, tdev->host_ip, tdev->host_port);
	
	// Create transceiver
	tdev->Transceiver = gtecnal_newTransceiver(&tdev->sockcmd);

	// Get session ID
	gtecnal_GetSessionID(tdev->Transceiver, tdev->SessionID);
	
	// Get connected devices
	int GetDevInfoOutput = gtecnal_GetConnectedDevices(tdev->Transceiver, tdev->SessionID, 0, tdev->ConnectedDevices);

	// Establish listening socket
	int ListenOnNetworkOutput = gtecnal_ListenOnNetwork(&tdev->socklisten, tdev->local_port);
	
	// Inform server for accepting endpoint
	int SetupStreamingOutput = gtecnal_SetupStreaming(tdev->Transceiver, tdev->SessionID, tdev->local_ip, tdev->local_port);
	
	//Accept incoming connection
	tdev->sockdata =  gtecnal_AcceptOnNetwork(tdev->socklisten, TCP_RECV_BUF_SIZE);
	if(tdev->sockdata == SOCKET_ERROR){
		fprintf(stderr,"Failed to connect, closing sockets!\n");
		gtecnal_CloseNetworkConnection(&tdev->socklisten);
		gtecnal_CloseNetworkConnection(&tdev->sockcmd);
	}

	// Open acquisition session
	int OpenAcqSesOutput = gtecnal_OpenDAQSession(tdev->Transceiver, tdev->SessionID, tdev->ConnectedDevices, 1, 1,tdev->DeviceType,tdev->DeviceName);
	
	// Specify number and type of channels, hardcoded according to CNBI conventions
	if(strcmp(tdev->DeviceType,"gUSBamp")==0){
	    tdev->NchannelsALL = 17;
	    tdev->NchannelsEEG = 16;
	    tdev->NchannelsEXG = 0;
	    tdev->NchannelsTRIG = 1;
	}else if(strcmp(tdev->DeviceType,"gHIamp")==0){
	    tdev->NchannelsALL = 81;
	    tdev->NchannelsEEG = 64;
	    tdev->NchannelsEXG = 16;
	    tdev->NchannelsTRIG = 1;
	}else if(strcmp(tdev->DeviceType,"gNautilus")==0){
	    tdev->NchannelsALL = 33;
	    tdev->NchannelsEEG = 32;
	    tdev->NchannelsEXG = 0;
	    tdev->NchannelsTRIG = 1;
	}else{
	    fprintf(stderr,"Unkown, unsupported device, crashing!\n");
	    exit(EXIT_FAILURE);
	}

        //Configure the device (with default settings of the device used + selected sampling rate)
	if(strcmp(tdev->DeviceType,"gUSBamp")==0){
            tdev->devconf = (gUSBampConfig*)malloc(sizeof(gUSBampConfig)); // Careful, must be freed in the end
            gtecnal_SetDefaultgUSBamp((gUSBampConfig*)tdev->devconf,tdev->SamplingRate); // TO REVIEW
	}else if(strcmp(tdev->DeviceType,"gHIamp")==0){
            tdev->devconf = (gHIampConfig*)malloc(sizeof(gHIampConfig)); // Careful, must be freed in the end
            gtecnal_SetDefaultgHIamp((gHIampConfig*)tdev->devconf,tdev->SamplingRate,tdev->BPFilter,tdev->notchFilter);
	}else if(strcmp(tdev->DeviceType,"gNautilus")==0){
            tdev->devconf = (gNautilusConfig*)malloc(sizeof(gNautilusConfig)); // Careful, must be freed in the end
            gtecnal_SetDefaultgNautilus((gNautilusConfig*)tdev->devconf,tdev->SamplingRate);
	}else{
	    fprintf(stderr,"Unkown, unsupported device, crashing!\n");
	    exit(EXIT_FAILURE);
	}

	char UsedDevConf[100000];
	int ConfDevOutput = gtecnal_SetConfiguration(tdev->Transceiver, tdev->SessionID, tdev->ConnectedDevices, 1, (DevConfig*)tdev->devconf, UsedDevConf);
	
	if (ConfDevOutput != 0)
            exit(EXIT_FAILURE); /* indicate failure.*/

        char GetConfig[100000];
	int GetConfigurationOutput = gtecnal_GetConfiguration(tdev->Transceiver, tdev->SessionID, 1, GetConfig);
	
        // Get data info
	int foundChannelsALL = 0;
	gtecnal_GetDataInfo(tdev->Transceiver, tdev->SessionID, tdev->ScanInfo, &tdev->DataPointsPerScan, &foundChannelsALL, &tdev->ScanCount);

    	// Globalize Sampling Rate and calculate ScansPerFrame
    	tdev->SamplingRate = tdev->devconf->sample_rate;
	if( ( (tdev->SamplingRate % 10) == 0 )) { // This means the SF is multiple of 10 rather than of 2
		if( tdev->SamplingRate <= 500) {
			tdev->ScansPerFrame = (int)(tdev->SamplingRate/2); // Replace 16 Hz for 10 Hz when SF is multiple of 10
		} else {
			tdev->ScansPerFrame = (int)(tdev->SamplingRate/2); // Be more conservative (5 Hz) for high sampling rates
		}
	} else { // Sampling rate is a multiple of 2 Hz and for the gTec devices goes only up to 512 Hz
		//tdev->ScansPerFrame = (int)(tdev->SamplingRate/16);// 16 Hz for multiples of 2
		tdev->ScansPerFrame = (int)(tdev->SamplingRate/4);// 4 Hz for multiples of 2
	}
	
    	int retSetCap = gtecnet_set_capability(tdev);
	
	tdev->runacq = 1;
    	// Open Reading thread
	int threadretval = pthread_create(&tdev->thid, NULL, gtecnet_read_fn, tdev);

  	if (threadretval < 0) {
		gtecnet_close_device(dev);
		//Check if I need to destroy anything in case of failure
		fprintf(stderr,"Open failed\n");
		return -1;
	}
	return 0;
}
/******************************************************************
 *               g.NEEDaccess methods implementation                	  *
 ******************************************************************/



static
int gtecnet_open_device(struct devmodule* dev, const char* optv[])
{
        if (init_data_com(dev,optv))
	{
		gtecnet_close_device(dev);
		return -1;
	}

	return 0;
}



static
int gtecnet_set_channel_groups(struct devmodule* dev, unsigned int ngrp,
					const struct grpconf* grp)
{

	struct gtecnet_eegdev* gtecnetdev = get_gtecnet(dev);
	struct selected_channels* selch;
	int i, nsel = 0;

	nsel = egdi_split_alloc_chgroups(dev, gtecnetdev->chmap,
	                                 ngrp, grp, &selch);
	for (i=0; i<nsel; i++)
		selch[i].bsc = 0;

	return (nsel < 0) ? -1 : 0;

}


static void gtecnet_fill_chinfo(const struct devmodule* dev, int stype,
	                     unsigned int ich, struct egd_chinfo* info)
{
	struct gtecnet_eegdev* tdev = get_gtecnet(dev);
	pthread_mutex_lock(&tdev->mtx);
	
	if(tdev->usedefmap){
	    if(strcmp(tdev->DeviceType,"gUSBamp")==0){
		if(stype == EGD_EEG)
		    info->label = gtecnetlabelUSBamp[ich];
		if(stype == EGD_SENSOR)
		    info->label = gtecnetlabelUSBamp[ich + tdev->NchannelsEEG];
		if(stype == EGD_TRIGGER)
		    info->label = gtecnetlabelUSBamp[ich + tdev->NchannelsEEG + tdev->NchannelsEXG];
	    }else if(strcmp(tdev->DeviceType,"gHIamp")==0){
		if(stype == EGD_EEG)
		    info->label = gtecnetlabelHIamp[ich];
		if(stype == EGD_SENSOR)
		    info->label = gtecnetlabelHIamp[ich + tdev->NchannelsEEG];
		if(stype == EGD_TRIGGER)
		    info->label = gtecnetlabelHIamp[ich + tdev->NchannelsEEG + tdev->NchannelsEXG];
	    }else if(strcmp(tdev->DeviceType,"gNautilus")==0){
		if(stype == EGD_EEG)
		    info->label = gtecnetlabelNautilus[ich];
		if(stype == EGD_SENSOR)
		    info->label = gtecnetlabelNautilus[ich + tdev->NchannelsEEG];
		if(stype == EGD_TRIGGER)
		    info->label = gtecnetlabelNautilus[ich + tdev->NchannelsEEG + tdev->NchannelsEXG];
	    }else{
		info->label = gtecnetlabelGen[ich];
	    }
	}else{
	    info->label = gtecnetlabelGen[ich];
	}
	pthread_mutex_unlock(&tdev->mtx);
	if (stype != EGD_TRIGGER) {
		info->isint = 0;
		info->dtype = EGD_FLOAT;
		info->min.valfloat = -16384.0;
		info->max.valfloat = +16384.0;
		info->unit = gtecnetunit;
		info->transducter = gtecnettransducter;
		info->prefiltering = "Unknown";
	} else {
		info->isint = 0;
		info->dtype = EGD_FLOAT;
		info->min.valint32_t = -8388608;
		info->max.valint32_t = 8388607;
		info->unit = gtecnetunit_trigger;
		info->transducter = gtecnettransducter_trigger;
		info->prefiltering = "No Filtering";
	}
}

API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct gtecnet_eegdev),
	.open_device = 		gtecnet_open_device,
	.close_device = 	gtecnet_close_device,
	.set_channel_groups = 	gtecnet_set_channel_groups,
	.fill_chinfo = 		gtecnet_fill_chinfo,
	.supported_opts =	gtecnet_options
};

