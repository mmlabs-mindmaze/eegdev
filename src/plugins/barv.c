/*
    Copyright (C) 2010-2014  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>
    Serafeim Perdikis <serafeim.perdikis@epfl.ch>

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
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <time.h>
#include <arpa/inet.h>

#include <eegdev-pluginapi.h>
#include "RecorderRDA.h"
#include "device-helper.h"

struct barv_eegdev {
	struct devmodule dev;
	int datafd;
	int fs;
	unsigned int nch;
	struct egdi_chinfo* chmap;
	pthread_t thid;
	pthread_mutex_t mtx;
	bool cap_set;
	int port;
};

#define get_barv(dev_p) ((struct barv_eegdev*)(dev_p))


#define DEFAULTHOST	"10.66.99.11"
#define DEFAULTPORT	"51254"
/******************************************************************
 *                Elements missing from old API                	  *
 ******************************************************************/


struct egdi_signal_info {
    const char *unit, *transducer, *prefiltering;
    int isint, bsc, dtype, mmtype;
    double scale;
    union gval min, max;
};


struct egdi_chinfo {
	const char *label;
	const struct egdi_signal_info* si;
	int stype;
};

/* EGDCAP_NOCP_*: use pointer directly (do not copy data) */
#define EGDCAP_NOCP_CHMAP    0x00000001
#define EGDCAP_NOCP_DEVID    0x00000002
#define EGDCAP_NOCP_DEVTYPE    0x00000004
#define EGDCAP_NOCP_CHLABEL    0x00000008

struct blockmapping {
    int nch;
    int num_skipped;
    int skipped_stype;
    const struct egdi_chinfo* chmap;
    const struct egdi_signal_info* default_info;
};

struct plugincap {
    unsigned int sampling_freq;
    int flags;
    unsigned int num_mappings;
    const struct blockmapping* mappings;
    const char* device_type;
    const char* device_id;
};

//struct egdi_optname {
//    const char *name, *defvalue;
//};

static inline
void egdi_set_gval(union gval* dst, int type, double val)
{
    if (type == EGD_INT32)
        dst->valint32_t = val;
    else if (type == EGD_FLOAT)
        dst->valfloat = val;
    else if (type == EGD_DOUBLE)
        dst->valdouble = val;
}


/******************************************************************
 *                       BARV internals                     	  *
 ******************************************************************/

static const char* labeltemplate[EGD_NUM_STYPE] = {
	[EGD_EEG] = "eeg:%u", 
	[EGD_SENSOR] = "sensor:%u", 
	[EGD_TRIGGER] = "trigger:%u"
};
static const char analog_unit[] = "uV";
static const char trigger_unit[] = "Boolean";
static const char analog_transducter[] = "Active Electrode";
static const char trigger_transducter[] = "Triggers and Status";
static const char trigger_prefiltering[] = "No filtering";
static const char barv_device_type[] = "BrainAmp Recorder/RecView";


static
const struct egdi_signal_info barv_siginfo[2] = {
	{
	.unit = "uV", .transducer = "Active electrode Float",
	.isint = 0, .bsc = 0, .dtype = EGD_FLOAT,
	.mmtype = EGD_FLOAT, .min = {.valfloat=-16384.0},
	.max = {.valfloat=16384.0}
	},{
	.unit = "uV", .transducer = "Triggers and Status",
	.isint = 1, .bsc = 0, .dtype = EGD_INT32,
	.mmtype = EGD_INT32, .min = {.valint32_t=-8388608},
	.max = {.valint32_t=8388608}
	}
};


enum {OPT_HOST, OPT_PORT, NUMOPT};
static const struct egdi_optname barv_options[] = {
	[OPT_HOST] = {.name = "host", .defvalue = NULL},
	[OPT_PORT] = {.name = "port", .defvalue = "51254"},
	[NUMOPT] = {.name = NULL}
};


static
int barv_set_capability(struct barv_eegdev* barvdev, const char* guid, unsigned int sf)
{
	struct blockmapping barv_mapping = {.num_skipped = 0};
	barv_mapping.nch = barvdev->nch; // Account for trigger channel
	barv_mapping.chmap = barvdev->chmap;
	barv_mapping.default_info = barv_siginfo;

	struct plugincap cap = {
		.sampling_freq = sf,
		.num_mappings = 1,
		.mappings = &barv_mapping,
		.device_type = barv_device_type,
		.device_id = guid,
		.flags = EGDCAP_NOCP_CHMAP | EGDCAP_NOCP_DEVID | EGDCAP_NOCP_DEVTYPE | EGDCAP_NOCP_CHLABEL | 
		EGD_CAP_FS | EGD_CAP_TYPELIST
	};

	// And now downgrade to the stupid older interface
	struct devmodule* dev = &barvdev->dev;	

	struct systemcap capold = {.type_nch = {0}};

	for (unsigned int i=0; i<barvdev->nch; i++)
		capold.type_nch[barvdev->chmap[i].stype]++;
	capold.sampling_freq = sf;
	capold.device_type = barv_device_type;
	capold.device_id = guid;

	dev->ci.set_cap(dev, &capold);
	dev->ci.set_input_samlen(dev, ((int)barv_mapping.nch)*sizeof(int32_t));
	return 0;
}

int GetServerMessage(int socket, RDA_MessageHeader **ppHeader)
// Get message from server, if available
// returns 0 if no data, -1 if error, 1 if ok.
{
	RDA_MessageHeader header;
	char* pData = (char*)&header;
	int nLength = 0;
	bool bFirstRecv = true;
	int nResult = 0;

	// Retrieve header.
	while(nLength < sizeof(header))
	{
		int nReqLength = sizeof(header) - nLength;
		nResult = recv(socket, (char*)pData, nReqLength, 0);
		bFirstRecv = false;
		nLength += nResult;
		pData += nResult;
	}
	
	*ppHeader = (RDA_MessageHeader*)malloc(header.nSize);

	if (!*ppHeader) return -1;
	memcpy(*ppHeader, &header, sizeof(header));
	pData = (char*)*ppHeader + sizeof(header);
	nLength = 0;
	int nDatasize = header.nSize - sizeof(header);

	// Retrieve rest of block.
	while(nLength < nDatasize)
	{
		int nReqLength = nDatasize - nLength;
		nResult = recv(socket, (char*)pData, nReqLength, 0);
		if (nResult < 0) return nResult;
		nLength += nResult;
		pData += nResult;
	}
	
	return 1;
}

static
void* barv_read_fn(void *data)
{
	struct barv_eegdev* tdev = data;
	const struct core_interface* restrict ci = &tdev->dev.ci;

	RDA_MessageHeader* pHeader = NULL;
	int nResult = -1;
	RDA_MessageStart* pMsgStart = NULL;
	RDA_MessageData32* pMsgData32 = NULL;
	RDA_MessageData* pMsgData = NULL;
	while(1){
		// Read data here
		if(nResult = GetServerMessage(tdev->datafd, &pHeader) > 0){
		
			switch(pHeader->nType){
		
				case 1:
				{	
					// Start receiving and setup
					pMsgStart = (RDA_MessageStart*)pHeader;
					// Retrieve number of channels.
					unsigned int nChannels = pMsgStart->nChannels;
					tdev->nch = nChannels;
					// Retrieve sampling frequency
					int SamplingFreq = (int)((1000000.0f/(pMsgStart->dSamplingInterval)));
					pthread_mutex_lock(&tdev->mtx);
					tdev->fs = (int)SamplingFreq;
					struct egdi_chinfo *newchmap;
					newchmap = malloc(((tdev->nch)+1)*sizeof(struct egdi_chinfo)); //Add an extra ch for the trigger
					tdev->chmap = newchmap;
					// Channel infos, set pointer to start of channel string.
					char* pszChannelNames = (char*)pMsgStart->dResolutions + nChannels * sizeof(pMsgStart->dResolutions[0]);

					struct egdi_signal_info* siginf = malloc((nChannels+1)*sizeof(struct egdi_signal_info));

					for (ULONG i = 0; i < nChannels; i++)
					{
						// Retrieve channel names and resolutions.
						char* curLbl = (char*)malloc((strlen(pszChannelNames)+1)*sizeof(char));
						memcpy(curLbl,pszChannelNames,strlen(pszChannelNames)+1);
						
						// Set channel labels
						tdev->chmap[i].label = curLbl;
						
						// Set channel type
						tdev->chmap[i].stype = EGD_EEG;

						// Set channel signal info
						siginf[i] = barv_siginfo[0];
						siginf[i].scale = pMsgStart->dResolutions[i];//Scale individually for each channel	
						tdev->chmap[i].si = &siginf[i];

						// Set pointer to next entry
						pszChannelNames += strlen(pszChannelNames) + 1;	
					}

					
					// Now augment channels by one to accommodate trigger channel
					tdev->nch = tdev->nch + 1;

					// Add info for trigger channel
					char* trigStr = "Trigger";					
					char* trigLbl = (char*)malloc((strlen(trigStr)+1)*sizeof(char));
					memcpy(trigLbl,trigStr,strlen(trigStr)+1);
					tdev->chmap[nChannels].label = trigLbl;
					tdev->chmap[nChannels].stype = EGD_TRIGGER;
					siginf[nChannels] = barv_siginfo[1];
					siginf[nChannels].scale = 1;
					tdev->chmap[nChannels].si = &siginf[nChannels];					
								
					// Set device capabilities				
					barv_set_capability(tdev, pHeader->guid, (unsigned int)SamplingFreq);
					tdev->cap_set = true;
					pthread_mutex_unlock(&tdev->mtx);
					// Free the message memory allocated in GetServerMessage
					free(pHeader);
					break;
				}
				case 2:
				{
					pMsgData = (RDA_MessageData*)pHeader;
					unsigned int nCh = tdev->nch -1;
					ULONG nBlocksize = pMsgData->nPoints * nCh * sizeof(pMsgData->nData[0]);

					if(nBlocksize == 0) continue;

					ULONG nLength = 0;
					int32_t myMarkers[pMsgData->nPoints];
					for(ULONG p=0;p<pMsgData->nPoints;p++){
						myMarkers[p]=0;
					}

					if(pMsgData->nMarkers > 0){
						// Position of first marker, immediately after the data block.
						for(ULONG i=0;i<pMsgData->nMarkers;i++){	
							RDA_Marker* pMarker = (RDA_Marker*)((char*)pMsgData->nData + 
								pMsgData->nPoints * nCh * sizeof(pMsgData->nData[0])+ nLength);
							nLength+=pMarker->nSize;
							int32_t MarkerVal = 0;
							// Supporting only markers of type "Stimulus","Response","SyncStatus"
							if(strcmp(pMarker->sTypeDesc,"Stimulus") == 0){
								MarkerVal = atoi(pMarker->sTypeDesc +10);
							}else if(strcmp(pMarker->sTypeDesc,"Response") == 0){
								if(strcmp(pMarker->sTypeDesc + 9,"R128") == 0){
									MarkerVal = 100; // Ignore potential other "responses"
								}
							}else if(strcmp(pMarker->sTypeDesc,"SyncStatus") == 0){
								// Assume that if it is not off, it is on and we are fine
								if(strcmp(pMarker->sTypeDesc + 11,"Sync Off") == 0){
									MarkerVal = 1000; // Ignore potential other "SyncStatus"
								}
							}
							for(ULONG p=0;p<pMarker->nPoints;p++){
								// For any channel (usually trigs are for all, 0 or -1)
								myMarkers[pMarker->nPosition + p] += MarkerVal;
							}
						}
					}

					//Re-multiplex data to add trigger channel
					int dLength = (nCh+1) * pMsgData->nPoints * sizeof(int32_t);				
					char* newData = (char*)malloc(dLength);
					
					float* newDataFloat = (float*)newData;
					
					for(ULONG point=0;point<pMsgData->nPoints;point++){
						for(ULONG pch=0;pch<nCh;pch++){
							// Copy each EEG channel separately to convert data to int32 for this time point	
							//Get each point/channel number
							int16_t sourceNum = *((int16_t*)(pMsgData->nData) + (point*nCh+pch));
							float thisNum32 = ((float)sourceNum)*((float)tdev->chmap[pch].si->scale);
							*newDataFloat = thisNum32;
							newDataFloat++;
						}
						// Copy trigger channel for this time point, as is (int32_t)
						memcpy(newDataFloat,&myMarkers[point],sizeof(int32_t));
						newDataFloat++;
					}
					
					if (ci->update_ringbuffer(&(tdev->dev), (float*)newData, dLength))
						break;
					
					free(newData);
					// Free the message memory allocated in GetServerMessage
					free(pHeader);
					break;
				}
				case 3:
				{
					// Free the message memory allocated in GetServerMessage
					free(pHeader);
					break;
				}						
				case 4:
				{	
					pMsgData32 = (RDA_MessageData32*)pHeader;
					unsigned int nCh = tdev->nch -1;
					ULONG nBlocksize = pMsgData32->nPoints * nCh * sizeof(pMsgData32->fData[0]);
					if(nBlocksize == 0) continue;

					ULONG nLength = 0;
					int32_t myMarkers[pMsgData32->nPoints];
					for(ULONG p=0;p<pMsgData32->nPoints;p++){
						myMarkers[p]=0;
					}

					if(pMsgData32->nMarkers > 0){
						// Position of first marker, immediately after the data block.
						for(ULONG i=0;i<pMsgData32->nMarkers;i++){	
							RDA_Marker* pMarker = (RDA_Marker*)((char*)pMsgData32->fData + 
								pMsgData32->nPoints * nCh * sizeof(pMsgData32->fData[0])+ nLength);
							//nLength+=pMarker->nSize; Their fucking RDA is buggy, sending always 44 for RecView
							// Thus, I replace it with manual measurement of MarkerSize
							int RealLength = 16; // 4 integers of 4 bytes each, in the beginning
							char* MarkerType = pMarker->sTypeDesc;							
							RealLength+=strlen(MarkerType)+1;
							char* MarkerDesc = MarkerType + strlen(MarkerType)+1;
							RealLength+=strlen(MarkerDesc)+1;
							nLength+=RealLength;
							int32_t MarkerVal = 0;
							// Supporting only markers of type "Stimulus","Response","SyncStatus"
							if(strcmp(pMarker->sTypeDesc,"Stimulus") == 0){
								MarkerVal = atoi(pMarker->sTypeDesc +10);
							}else if(strcmp(pMarker->sTypeDesc,"Response") == 0){
								if(strcmp(pMarker->sTypeDesc + 9,"R128") == 0){
									MarkerVal = 100; // Ignore potential other "responses"
								}
							}else if(strcmp(pMarker->sTypeDesc,"SyncStatus") == 0){
								if(strcmp(pMarker->sTypeDesc + 11,"Sync Off") == 0){
									MarkerVal = 2000; // Off is a problem, mark with 2000
								}else if(strcmp(pMarker->sTypeDesc + 11,"Sync On") == 0){
									MarkerVal = 1000; // On is ok, mark with 1000
								}
							}

							for(ULONG p=0;p<pMarker->nPoints;p++){
								// For any channel (usually trigs are for all, 0 or -1)
								myMarkers[pMarker->nPosition + p] += MarkerVal;
							}
						}
					}
					
					//Re-multiplex data to add trigger channel
					int dLength = (nCh+1) * pMsgData32->nPoints * sizeof(int32_t);				
					char* newData = (char*)malloc(dLength);
					
					float* newDataFloat = (float*)newData;
					
					for(ULONG point=0;point<pMsgData32->nPoints;point++){
						for(ULONG pch=0;pch<nCh;pch++){
							// Copy each EEG channel separately to convert data to int32 for this time point	
							//Get each point/channel number
							float sourceNum = *((float*)(pMsgData32->fData) + (point*nCh+pch));
							float thisNum32 = ((float)sourceNum)*((float)tdev->chmap[pch].si->scale);
							*newDataFloat = thisNum32;
							newDataFloat++;
						}
						// Copy trigger channel for this time point, as is (int32_t)
						//*newDataFloat = myMarkers[point];
						memcpy(newDataFloat,&myMarkers[point],sizeof(int32_t));
						newDataFloat++;
					}
					
					if (ci->update_ringbuffer(&(tdev->dev), (float*)newData, dLength))
						break;
					free(newData);
					// Free the message memory allocated in GetServerMessage
					free(pHeader);
					break;
				}
			}
			
		}
	}
	// We can reach here only if there was an error previously
	ci->report_error(&tdev->dev, errno);
	return NULL;
}



/*****************************************************************
 *                        barv misc                            *
 *****************************************************************/
static
int parse_url(const char* url, char* host, unsigned short *port)
{

	if (!sscanf(url, "%[^][:]:%hu", host, port)
	 && !sscanf(url, "%[:0-9a-f]", host)
	 && !sscanf(url, "[%[:0-9a-f]]:%hu", host, port)) {
		fprintf(stderr, "Cannot parse address\n");
		return -1;
	}

	return 0;
}


static
int connect_server(const char *host, unsigned int short port)
{
	struct addrinfo *ai, *res, hints = {.ai_socktype = SOCK_STREAM};
	int fd, error, family, socktype, proto;
	char portnum[8];
	
	// Name resolution
	snprintf(portnum, sizeof(portnum), "%u", port);
	if ((error = getaddrinfo(host, portnum, &hints, &res))) {
		fprintf(stderr, "failed: %s\n", gai_strerror(error));
		return -1;
	}

	// Create and connect socket (loop over all possible addresses)
	for (ai=res; ai != NULL; ai = ai->ai_next) {
		family = ai->ai_family;
		socktype = ai->ai_socktype | SOCK_CLOEXEC;
		proto = ai->ai_protocol;

		if ((fd = socket(family, socktype, proto)) < 0
		  || connect(fd, res->ai_addr, res->ai_addrlen)) {
			if (fd >= 0)
				close(fd);
			fd = -1;
		} else
			break;
	}

	freeaddrinfo(res);
	return fd;
}

static
int init_data_com(struct barv_eegdev* tdev, const char* host, int port)
{
	struct timespec tim;
	tim.tv_sec = 0;
	tim.tv_nsec = 500000000;
	struct devmodule* dev = &tdev->dev;

	tdev->datafd = connect_server(host, port);
	tdev->port = port;
  	if (tdev->datafd < 0) {
		close(tdev->datafd);
		tdev->datafd = -1;
		return -1;
	}
	
	// Give it some time
	nanosleep(&tim,NULL);

	int threadretval = pthread_create(&tdev->thid, NULL, barv_read_fn, tdev); 
  	if (threadretval < 0) {
		close(tdev->datafd);
		tdev->datafd = -1;
		return -1;
	}

	return 0;
}

/******************************************************************
 *               BARV methods implementation                	  *
 ******************************************************************/

static
int barv_close_device(struct devmodule* dev)
{
	struct barv_eegdev* tdev = get_barv(dev);
	unsigned int i;

	// Free channels metadata
	free(tdev->chmap);

	// Destroy data connection
	if (tdev->datafd >= 0) {
		pthread_cancel(tdev->thid);
		pthread_join(tdev->thid, NULL);
		close(tdev->datafd);
	}
}

static
int barv_open_device(struct devmodule* dev, const char* optv[])
{
	struct barv_eegdev* tdev = get_barv(dev);
	unsigned short port = atoi(optv[OPT_PORT]);
	const char *url = optv[OPT_HOST];
	size_t hostlen = url ? strlen(url) : 0;
	char hoststring[hostlen + 1];
	char* host = url ? hoststring : NULL;

	fprintf(stdout,"Host = %s\n",host);
	fprintf(stdout,"Port = %d\n",port);

	tdev->datafd = -1;
	pthread_mutex_lock(&tdev->mtx);
	tdev->cap_set = false;
	pthread_mutex_unlock(&tdev->mtx);

	if ( (url && parse_url(url, host, &port))
	  || init_data_com(tdev, host, port) ) 
	{	
		barv_close_device(dev);
		return -1;
	}

	// Wait here until we are sure all is set and running
	// This is due to the fact that I need to read the first
	// message in order to setup the device
	bool allset = false;
	while(true){
		pthread_mutex_lock(&tdev->mtx);
		allset = tdev->cap_set;
		pthread_mutex_unlock(&tdev->mtx);
		if(allset) {
			break;
		}
	}

	return 0;
}

static 
int barv_set_channel_groups(struct devmodule* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	struct barv_eegdev* tdev = get_barv(dev);
	pthread_mutex_lock(&tdev->mtx);
	struct selected_channels* selch;
	int i, nsel = 0;

	// Downgrade from egdi_chinfo to egdich struct
	struct egdich oldchmap[tdev->nch];
	for(unsigned int i=0;i<tdev->nch;i++){
		oldchmap[i].label = tdev->chmap[i].label;
		oldchmap[i].stype = tdev->chmap[i].stype;
		oldchmap[i].dtype = tdev->chmap[i].si->dtype;
		oldchmap[i].data = (void*)tdev->chmap[i].si;
	} 
	nsel = egdi_split_alloc_chgroups(dev, oldchmap,
	                                 ngrp, grp, &selch);
	for (i=0; i<nsel; i++)
		selch[i].bsc = 0;
	pthread_mutex_unlock(&tdev->mtx);
	return (nsel < 0) ? -1 : 0;
}


static 
void barv_fill_chinfo(const struct devmodule* dev, int stype,
	                     unsigned int ich, struct egd_chinfo* info)
{
	struct barv_eegdev* tdev = get_barv(dev);
	pthread_mutex_lock(&tdev->mtx);

	info->label = tdev->chmap[ich].label;
	pthread_mutex_unlock(&tdev->mtx);
	if (stype != EGD_TRIGGER) {
		info->isint = 0;
		info->dtype = EGD_FLOAT;
		info->min.valfloat = -16384.0;
		info->max.valfloat = +16384.0;
		info->unit = analog_unit;
		info->transducter = analog_transducter;
		info->prefiltering = "Unknown";
	} else {
		info->isint = 1;
		info->dtype = EGD_INT32;
		info->min.valint32_t = -8388608;
		info->max.valint32_t = 8388607;
		info->unit = trigger_unit;
		info->transducter = trigger_transducter;
		info->prefiltering = trigger_prefiltering;
	}
}

API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct barv_eegdev),
	.open_device = 		barv_open_device,
	.close_device = 	barv_close_device,
	.set_channel_groups = 	barv_set_channel_groups,
	.fill_chinfo = 		barv_fill_chinfo,
	.supported_opts = 	barv_options
};

