/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <libusb.h>
#include "usb_comm.h"

#include "eegdev-types.h"
#include "eegdev-common.h"
#include "devices.h"

// It should ABSOLUTELY be a power of two or the read call will fail
#define CHUNKSIZE	(64*1024)
typedef const char  label4_t[4];


struct act2_eegdev {
	struct eegdev dev;
	sem_t hd_init;
	pthread_t thread_id;
	pthread_mutex_t acqlock;
	int runacq;
	unsigned int offset[EGD_NUM_STYPE];
	char prefiltering[32];

	label4_t* eeglabel;

	// USB communication related
	libusb_device_handle* hudev;
	struct usb_btransfer ubtr;
};

#define get_act2(dev_p) \
	((struct act2_eegdev*)(((char*)(dev_p))-offsetof(struct act2_eegdev, dev)))

#define data_in_sync(pdata)	(*((const uint32_t*)(pdata)) == 0xFFFFFF00)

// Biosemi methods declaration
static int act2_close_device(struct eegdev* dev);
static int act2_noaction(struct eegdev* dev);
static int act2_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);
static void act2_fill_chinfo(const struct eegdev* dev, int stype,
	                     unsigned int ich, struct egd_chinfo* info);

static const struct eegdev_operations biosemi_ops = {
	.close_device = act2_close_device,
	.start_acq = act2_noaction,
	.stop_acq = act2_noaction,
	.set_channel_groups = act2_set_channel_groups,
	.fill_chinfo = act2_fill_chinfo
};


/*****************************************************************
 *                     System capabilities                       *
 *****************************************************************/
static const unsigned short samplerates[2][9] = {
	{2048, 4096, 8192, 16384, 2048, 4096, 8192, 16384, 2048},
	{2048, 2048, 2048, 2048, 2048, 4096, 8192, 16384, 2048}
};
static const unsigned short sample_array_sizes[2][9] = {
	{258, 130, 66, 34, 258, 130, 66, 34, 290}, 
	{610, 610, 610, 610, 282, 154, 90, 58, 314}
}; 
static const unsigned short num_eeg_channels[2][9] = {
	{256, 128, 64, 32, 232, 104, 40, 8, 256}, 
	{512, 512, 512, 512, 256, 128, 64, 32, 280}
}; 

static const union gval act2_scales[EGD_NUM_DTYPE] = {
	[EGD_INT32] = {.valint32_t = 1},
	[EGD_FLOAT] = {.valfloat = (1.0f/8192.0f)},
	[EGD_DOUBLE] = {.valdouble = (1.0/8192.0)},
};

static const char trigg_prefiltering[] = "No filtering";

static label4_t eeg256label[] = {
	"A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10", "A11",
	"A12", "A13", "A14", "A15", "A16", "A17", "A18", "A19", "A20",
	"A21", "A22", "A23", "A24", "A25", "A26", "A27", "A28", "A29",
	"A30", "A31", "A32",
	"B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "B10", "B11",
	"B12", "B13", "B14", "B15", "B16", "B17", "B18", "B19", "B20",
	"B21", "B22", "B23", "B24", "B25", "B26", "B27", "B28", "B29",
	"B30", "B31", "B32",
	"C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "C10", "C11",
	"C12", "C13", "C14", "C15", "C16", "C17", "C18", "C19", "C20",
	"C21", "C22", "C23", "C24", "C25", "C26", "C27", "C28", "C29",
	"C30", "C31", "C32",
	"D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D10", "D11",
	"D12", "D13", "D14", "D15", "D16", "D17", "D18", "D19", "D20",
	"D21", "D22", "D23", "D24", "D25", "D26", "D27", "D28", "D29",
	"D30", "D31", "D32",
	"E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "E9", "E10", "E11",
	"E12", "E13", "E14", "E15", "E16", "E17", "E18", "E19", "E20",
	"E21", "E22", "E23", "E24", "E25", "E26", "E27", "E28", "E29",
	"E30", "E31", "E32",
	"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11",
	"F12", "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20",
	"F21", "F22", "F23", "F24", "F25", "F26", "F27", "F28", "F29",
	"F30", "F31", "F32",
	"G1", "G2", "G3", "G4", "G5", "G6", "G7", "G8", "G9", "G10", "G11",
	"G12", "G13", "G14", "G15", "G16", "G17", "G18", "G19", "G20",
	"G21", "G22", "G23", "G24", "G25", "G26", "G27", "G28", "G29",
	"G30", "G31", "G32",
	"H1", "H2", "H3", "H4", "H5", "H6", "H7", "H8", "H9", "H10", "H11",
	"H12", "H13", "H14", "H15", "H16", "H17", "H18", "H19", "H20",
	"H21", "H22", "H23", "H24", "H25", "H26", "H27", "H28", "H29",
	"H30", "H31", "H32"
};

static label4_t eeg64label[] = {
	"Fp1","AF7","AF3","F1","F3","F5","F7","FT7",
	"FC5","FC3","FC1","C1","C3","C5","T7","TP7",
	"CP5","CP3","CP1","P1","P3","P5","P7","P9",
	"PO7","PO3","O1","Iz","Oz","POz","Pz","CPz",
	"Fpz","Fp2","AF8","AF4","AFz","Fz","F2","F4",
	"F6","F8","FT8","FC6","FC4","FC2","FCz","Cz",
	"C2","C4","C6","T8","TP8","CP6","CP4","CP2",
	"P2","P4","P6","P8","P10","PO8","PO4","O2"
};

static label4_t eeg32label[] = {
	"Fp1", "AF3", "F7", "F3", "FC1", "FC5", "T7", "C3",
	"CP1", "CP5", "P7", "P3", "P9", "Pz", "PO3", "O1",
	"Oz", "O2", "PO4", "P4", "P8", "CP6", "CP2", "C4",
	"T8", "FC6", "FC2", "F4", "F8", "AF4", "FP2", "Fz",
	"Cz"
};


static const char sensorlabel[][8] = {
	"EXG1","EXG2","EXG3","EXG4","EXG5","EXG6","EXG7","EXG8",
	"sens1","sens2","sens3","sens4","ERGO1","sens6","sens7",
	"sens8","sens9","sens10","sens11","sens12","sens13","sens14",
	"sens15", "sens16"
};

static const char trigglabel[] = "Status";
static const char analog_unit[] = "uV";
static const char trigger_unit[] = "Boolean";
static const char analog_transducter[] = "Active Electrode";
static const char trigger_transducter[] = "Triggers and Status";
static const char model_type1[] = "Biosemi ActiveTwo Mk1";
static const char model_type2[] = "Biosemi ActiveTwo Mk2";
static const char device_id[] = "N/A";


/******************************************************************
 *                       USB interaction                          *
 ******************************************************************/
#define USB_ACTIVETWO_VENDOR_ID		0x0547
#define USB_ACTIVETWO_PRODUCT_ID	0x1005
#define ACT2_EP_OUT			0x01
#define ACT2_EP_IN			0x82
#define ACT2_TIMEOUT			200

static int act2_write(libusb_device_handle* hudev, void* buff, size_t size)
{
	int actual_length, ret;
	ret = libusb_bulk_transfer(hudev, ACT2_EP_OUT, buff, 
	                           size, &actual_length, ACT2_TIMEOUT);
	if (ret) {
		errno = ENODEV;
		return -1;
	}
	return actual_length;
}


static libusb_device_handle* act2_open_dev(void)
{
	libusb_device_handle *hudev;
	int ret = LIBUSB_ERROR_NO_DEVICE;

	hudev = libusb_open_device_with_vid_pid(egd_acquire_usb_context(),
					       USB_ACTIVETWO_VENDOR_ID,
					       USB_ACTIVETWO_PRODUCT_ID);
	if ( (hudev == NULL) 
	   || (ret = libusb_set_configuration(hudev, 1))
	   || (ret = libusb_claim_interface(hudev, 0))
	   || (ret = libusb_clear_halt(hudev, ACT2_EP_OUT)) 
	   || (ret = libusb_clear_halt(hudev, ACT2_EP_IN)))
		goto error;
	
	return hudev;


error:
	if (ret == LIBUSB_ERROR_BUSY)
		errno = EBUSY;
	else if (ret == LIBUSB_ERROR_NO_DEVICE)
		errno = ENODEV;
	else
		errno = EIO;

	if (hudev != NULL)
		libusb_close(hudev);
	egd_release_usb_context();
	return NULL;
}


static int act2_close_dev(libusb_device_handle* hudev)
{
	if (hudev != NULL) {
		libusb_release_interface(hudev, 0);
		libusb_close(hudev);
		egd_release_usb_context();
	}
	return 0;
}



/******************************************************************
 *                       Activetwo internals                      *
 ******************************************************************/
static int act2_interpret_triggers(struct act2_eegdev* a2dev, uint32_t tri)
{
	unsigned int arr_size, mode, mk, eeg_nmax;

	// Determine speedmode
	mode = (tri & 0x0E000000) >> 25;
	if (tri & 0x20000000)
		mode += 8;

	// Determine model
	mk = (tri & 0x80000000) ? 2 : 1;

	// Determine sampling frequency and the maximum number of EEG and
	// sensor channels
	arr_size = sample_array_sizes[mk-1][mode];
	a2dev->dev.cap.sampling_freq = samplerates[mk-1][mode];
	eeg_nmax = num_eeg_channels[mk-1][mode];
	a2dev->dev.cap.type_nch[EGD_EEG] = eeg_nmax;
	a2dev->dev.cap.type_nch[EGD_SENSOR] = arr_size - eeg_nmax - 2;
	a2dev->dev.cap.type_nch[EGD_TRIGGER] = 1;
	a2dev->offset[EGD_EEG] = 2*sizeof(int32_t);
	a2dev->offset[EGD_SENSOR] = (2+eeg_nmax)*sizeof(int32_t);
	a2dev->offset[EGD_TRIGGER] = 5;
	a2dev->dev.cap.device_type = (mk==1) ? model_type1 : model_type2; 
	a2dev->dev.cap.device_id = device_id; 

	// Fill the prefiltering field
	sprintf(a2dev->prefiltering, "HP: DC; LP: %.1f Hz",
	        (double)(a2dev->dev.cap.sampling_freq / 4.9112));

	egd_set_input_samlen(&(a2dev->dev), arr_size*sizeof(int32_t));

	return 0;
}


static void* multiple_sweeps_fn(void* arg)
{
	struct act2_eegdev* a2dev = arg;
	struct usb_btransfer* ubtr = &(a2dev->ubtr);
	char* chunkbuff = NULL;
	ssize_t rsize = 0;
	int i, samstart, in_samlen, runacq;
	
	if (egd_start_usb_btransfer(ubtr)) 
		goto endinit;
		
	rsize = egd_swap_usb_btransfer(ubtr, &chunkbuff);
	if (rsize < 2 || !data_in_sync(chunkbuff)) {
		egd_report_error(&(a2dev->dev), (rsize < 0) ? errno : EIO);
		goto endinit;
	}
	act2_interpret_triggers(a2dev, ((uint32_t*)chunkbuff)[1]);
	
endinit:
	// signals handshake has been enabled (or failed)
	sem_post(&(a2dev->hd_init));
	
	in_samlen = a2dev->dev.in_samlen;
	while (rsize > 0) {
		pthread_mutex_lock(&(a2dev->acqlock));
		runacq = a2dev->runacq;
		pthread_mutex_unlock(&(a2dev->acqlock));
		if (!runacq)
			break;

		// check presence synchro code
		samstart = (in_samlen - a2dev->dev.in_offset) % in_samlen;
		for (i=samstart; i<rsize; i+=in_samlen) {
			if (!data_in_sync(chunkbuff+i)) {
				egd_report_error(&(a2dev->dev), EIO);
				return NULL;
			}
		}

		// Update the eegdev structure with the new data
		if (egd_update_ringbuffer(&(a2dev->dev), chunkbuff, rsize))
			break;

		// Read data from the USB device
		rsize = egd_swap_usb_btransfer(ubtr, &chunkbuff);
		if (rsize < 0) {
			egd_report_error(&(a2dev->dev), errno);
			break;
		}
	}

	egd_stop_usb_btransfer(ubtr);
	return NULL;
}


static int act2_disable_handshake(struct act2_eegdev* a2dev)
{
	unsigned char usb_data[64] = {0};
	
	//pthread_cancel(a2dev->thread_id);
	pthread_mutex_lock(&(a2dev->acqlock));
	a2dev->runacq = 0;
	pthread_mutex_unlock(&(a2dev->acqlock));
	
	pthread_join(a2dev->thread_id, NULL);

	usb_data[0] = 0x00;
	act2_write(a2dev->hudev, usb_data, 64);
	return 0;
}



static int act2_enable_handshake(struct act2_eegdev* a2dev)
{
	unsigned char usb_data[64] = {0};
	int retval, error;

	// Init activetwo USB comm
	usb_data[0] = 0x00;
	act2_write(a2dev->hudev, usb_data, 64);

	// Start reading from activetwo device
	a2dev->runacq = 1;
	retval = pthread_create(&(a2dev->thread_id), NULL,
	                        multiple_sweeps_fn, a2dev);
	if (retval) {
		a2dev->runacq = 0;
		errno = retval;
		return -1;
	}

	// Start handshake
	usb_data[0] = 0xFF;
	act2_write(a2dev->hudev, usb_data, 64);
	

	// wait for the handshake completion
	sem_wait(&(a2dev->hd_init));

	// Check that handshake has been enabled
	pthread_mutex_lock(&(a2dev->acqlock));
	error = a2dev->dev.error;
	pthread_mutex_unlock(&(a2dev->acqlock));
	if (error) {
		act2_disable_handshake(a2dev);	
		errno = error;
		return -1;
	}

	return 0;
}


static int init_act2dev(struct act2_eegdev* a2dev, unsigned int nch)
{
 	libusb_device_handle* hudev = NULL;
	
	if (!(hudev = act2_open_dev()))
		return -1;
		
 	if (egd_init_eegdev(&(a2dev->dev), &biosemi_ops))
		goto error;

	if (egd_init_usb_btransfer(&(a2dev->ubtr), hudev, ACT2_EP_IN,
				   CHUNKSIZE, ACT2_TIMEOUT))
		goto error2;
	
	pthread_mutex_init(&(a2dev->acqlock), NULL);
	sem_init(&(a2dev->hd_init), 0, 0);
	a2dev->runacq = 0;
	a2dev->hudev = hudev;
	if (nch == 32)
		a2dev->eeglabel = eeg32label; 
	else if (nch == 64)
		a2dev->eeglabel = eeg64label;
	else
		a2dev->eeglabel = eeg256label;
	return 0;

error2:
	egd_destroy_eegdev(&(a2dev->dev));
error:
	act2_close_dev(hudev);
	return -1; 
}


static void destroy_act2dev(struct act2_eegdev* a2dev)
{
	if (a2dev == NULL)
		return;

	sem_destroy(&(a2dev->hd_init));
	pthread_mutex_destroy(&(a2dev->acqlock));
	egd_destroy_usb_btransfer(&(a2dev->ubtr));
	egd_destroy_eegdev(&(a2dev->dev));
	act2_close_dev(a2dev->hudev);
}


/******************************************************************
 *               Activetwo methods implementation                 *
 ******************************************************************/
LOCAL_FN
struct eegdev* open_biosemi(const char* optv[])
{
	unsigned int nch = atoi(egd_getopt("numch", "64", optv));
	struct act2_eegdev* a2dev = NULL;

	if (nch != 32 && nch != 64 && nch != 128 && nch != 256) {
		errno = EINVAL;
		return NULL;
	}

	// alloc and initialize tructure
	if ( !(a2dev = malloc(sizeof(*a2dev)))
	    || init_act2dev(a2dev, nch) )
		goto error;

	// Start the communication
	if (!act2_enable_handshake(a2dev)) {
		if (nch < a2dev->dev.cap.type_nch[EGD_EEG])
			a2dev->dev.cap.type_nch[EGD_EEG] = nch;
		return &(a2dev->dev);
	}
	egd_update_capabilities(&(a2dev->dev));

	//If we reach here, the communication has failed
	destroy_act2dev(a2dev);

error:
	free(a2dev);
	return NULL;
}


static int act2_close_device(struct eegdev* dev)
{
	struct act2_eegdev* a2dev = get_act2(dev);
	
	act2_disable_handshake(a2dev);
	destroy_act2dev(a2dev);
	free(a2dev);

	return 0;
}


static
int act2_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
                            const struct grpconf* grp)
{
	unsigned int i, stype;
	struct selected_channels* sch = dev->selch;
	struct act2_eegdev* a2dev = get_act2(dev);
	
	for (i=0; i<ngrp; i++) {
		stype = grp[i].sensortype;
		// Set parameters of (eeg -> ringbuffer)
		sch[i].in_offset = a2dev->offset[stype]
		                   + grp[i].index*sizeof(int32_t);
		sch[i].inlen = grp[i].nch*sizeof(int32_t);
		sch[i].bsc = (stype == EGD_TRIGGER) ? 0 : 1;
		sch[i].sc = act2_scales[grp[i].datatype];
		sch[i].typein = EGD_INT32;
	}
		
	return 0;
}


static void act2_fill_chinfo(const struct eegdev* dev, int stype,
	                     unsigned int ich, struct egd_chinfo* info)
{
	if (stype != EGD_TRIGGER) {
		info->isint = 0;
		info->dtype = EGD_DOUBLE;
		info->min.valdouble = -262144.0;
		info->max.valdouble = 262143.96875;
		info->label = (stype == EGD_EEG) ? 
					eeg64label[ich] : sensorlabel[ich];
		info->unit = analog_unit;
		info->transducter = analog_transducter;
		info->prefiltering = get_act2(dev)->prefiltering; 
	} else {
		info->isint = 1;
		info->dtype = EGD_INT32;
		info->min.valint32_t = -8388608;
		info->max.valint32_t = 8388607;
		info->label = trigglabel;
		info->unit = trigger_unit;
		info->transducter = trigger_transducter;
		info->prefiltering = trigg_prefiltering;
	}
}


static int act2_noaction(struct eegdev* dev)
{
	(void)dev;
	return 0;
}

