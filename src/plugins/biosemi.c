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
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <pthread.h>
#include <byteswap.h>
#include <libusb.h>

#include <eegdev-pluginapi.h>

#ifndef le_to_cpu_32
# if WORDS_BIGENDIAN
#  define le_to_cpu_u32(data)	bswap_32(data)
# else
#  define le_to_cpu_u32(data)	(data)
# endif //WORDS_BIGENDIAN
#endif

// It should ABSOLUTELY be a power of two or the read call will fail
#define CHUNKSIZE	(64*1024)
#define NUMURB		2
typedef const char  label4_t[4];


struct act2_eegdev {
	struct devmodule dev;
	unsigned int offset[EGD_NUM_STYPE];
	char prefiltering[32];

	unsigned int optnch;
	label4_t* eeglabel;

	int samplelen;	//number of int32 in a time sample
	int inoffset;	//offset in next chunk of a sample (in num of int32)

	// USB communication related
	pthread_t thread_id;
	pthread_cond_t cond;
	pthread_mutex_t mtx;
	int stopusb, resubmit, num_running;
	libusb_context* ctx;
	libusb_device_handle* hudev;
	struct libusb_transfer* urb[NUMURB];
};


#define get_act2(dev_p) ((struct act2_eegdev*)(((char*)(dev_p))))

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

static const struct egdi_optname act2_options[] = {
	{.name = "numch", .defvalue = "64"},
	{.name = NULL}
};

/******************************************************************
 *                       USB interaction                          *
 ******************************************************************/
#define USB_ACTIVETWO_VENDOR_ID		0x0547
#define USB_ACTIVETWO_PRODUCT_ID	0x1005
#define ACT2_EP_OUT			0x01
#define ACT2_EP_IN			0x82
#define ACT2_TIMEOUT			200

static
int proc_libusb_error(int libusbret)
{
	if (libusbret == 0)
		return 0;
	else if (libusbret == LIBUSB_ERROR_TIMEOUT)
		return EAGAIN;
	else if (libusbret == LIBUSB_ERROR_BUSY)
		return EBUSY;
	else if (libusbret == LIBUSB_ERROR_NO_DEVICE)
		return ENODEV;
	else 
		return EIO;
}

static
int proc_libusb_transfer_ret(int ret)
{
	switch (ret) {
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_CANCELLED:
		return 0;

	case LIBUSB_TRANSFER_TIMED_OUT:
		return EAGAIN;

	case LIBUSB_TRANSFER_NO_DEVICE:
		return ENODEV;

	case LIBUSB_TRANSFER_ERROR:
	case LIBUSB_TRANSFER_OVERFLOW:
	case LIBUSB_TRANSFER_STALL:
	default:
		return EIO;
	}
}


static void* usb_event_handling_proc(void* arg)
{
	struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
	struct act2_eegdev* a2dev = arg;
	int quit;

	while (1) {
		pthread_mutex_lock(&a2dev->mtx);
		quit = a2dev->stopusb;
		pthread_mutex_unlock(&a2dev->mtx);
		if (quit)
			break;

		libusb_handle_events_timeout(a2dev->ctx, &tv);
	}

	return NULL;
}


static int act2_write(libusb_device_handle* hudev, void* buff, size_t size)
{
	int actual_length, ret;
	ret = libusb_bulk_transfer(hudev, ACT2_EP_OUT, buff, 
	                           size, &actual_length, ACT2_TIMEOUT);
	if (ret) {
		errno = proc_libusb_error(ret);
		return -1;
	}
	return actual_length;
}


static int act2_open_dev(struct act2_eegdev* a2dev)
{
	libusb_device_handle *hudev = NULL;
	libusb_context* ctx = NULL;
	int ret, errnum;

	// Initialize a session to libusb, open an Activetwo2 device
	// and initialize the endpoints
	if ( (ret = libusb_init(&ctx))
	   || !(hudev = libusb_open_device_with_vid_pid(ctx,
					       USB_ACTIVETWO_VENDOR_ID,
					       USB_ACTIVETWO_PRODUCT_ID))
	   || (ret = libusb_set_configuration(hudev, 1))
	   || (ret = libusb_claim_interface(hudev, 0))
	   || (ret = libusb_clear_halt(hudev, ACT2_EP_OUT)) 
	   || (ret = libusb_clear_halt(hudev, ACT2_EP_IN)) )
		goto error;

	a2dev->ctx = ctx;
	a2dev->hudev = hudev;
	
	if (pthread_cond_init(&a2dev->cond, NULL)
	  || pthread_mutex_init(&a2dev->mtx, NULL)
	  || pthread_create(&a2dev->thread_id, NULL,
	                    usb_event_handling_proc, a2dev))
		goto error;
	
	return 0;


error:
	if (hudev)
		libusb_close(hudev);
	if (ctx)
		libusb_exit(ctx);
	errnum = proc_libusb_error(ret);
	errno = errnum ? errnum : EIO;
	return -1;
}


static int act2_close_dev(struct act2_eegdev* a2dev)
{
	// Close the USB device
	if (a2dev->hudev != NULL) {
		libusb_release_interface(a2dev->hudev, 0);
		libusb_close(a2dev->hudev);
	}

	// Close the session to libusb
	if (a2dev->ctx) {
		pthread_mutex_lock(&a2dev->mtx);
		a2dev->stopusb = 1;
		pthread_mutex_unlock(&a2dev->mtx);
		pthread_join(a2dev->thread_id, NULL);
		pthread_mutex_destroy(&a2dev->mtx);
		pthread_cond_destroy(&a2dev->cond);
		libusb_exit(a2dev->ctx);
	}
	return 0;
}



/******************************************************************
 *                       Activetwo internals                      *
 ******************************************************************/
static int parse_triggers(struct act2_eegdev* a2dev, uint32_t tri)
{
	unsigned int arr_size, mode, mk, eeg_nmax;
	struct systemcap cap;
	int samlen;
	struct devmodule* dev = &a2dev->dev;

	// Determine speedmode
	mode = (tri & 0x0E000000) >> 25;
	if (tri & 0x20000000)
		mode += 8;

	// Determine model
	mk = (tri & 0x80000000) ? 2 : 1;

	// Determine sampling frequency and the maximum number of EEG and
	// sensor channels
	arr_size = sample_array_sizes[mk-1][mode];
	eeg_nmax = num_eeg_channels[mk-1][mode];
	a2dev->offset[EGD_EEG] = 2*sizeof(int32_t);
	a2dev->offset[EGD_SENSOR] = (2+eeg_nmax)*sizeof(int32_t);
	a2dev->offset[EGD_TRIGGER] = 4;
	a2dev->samplelen = arr_size;

	// Set the capabilities
	cap.device_type = (mk==1) ? model_type1 : model_type2;
	cap.device_id = device_id;
	cap.sampling_freq = samplerates[mk-1][mode];
	cap.type_nch[EGD_EEG] = eeg_nmax;
	cap.type_nch[EGD_SENSOR] = arr_size - eeg_nmax - 2;
	cap.type_nch[EGD_TRIGGER] = 1;
	if (a2dev->optnch < cap.type_nch[EGD_EEG])
			cap.type_nch[EGD_EEG] = a2dev->optnch;
	dev->ci.set_cap(dev, &cap);

	// Fill the prefiltering field
	snprintf(a2dev->prefiltering, sizeof(a2dev->prefiltering),
	        "HP: DC; LP: %.1f Hz", (double)(cap.sampling_freq/4.9112));

	samlen = arr_size*sizeof(int32_t);
	dev->ci.set_input_samlen(dev, samlen);
	return samlen;
}


static
void process_usbbuf(struct act2_eegdev* a2dev, uint32_t* buf, ssize_t bs)
{
	int i, start, slen = a2dev->samplelen, inoffset = a2dev->inoffset;
	const struct core_interface* ci = &(a2dev->dev.ci);

#if WORDS_BIGENDIAN
	for (i=0; i<bs; i++)
		buf[i] = bswap_32(buf[i]);
#endif //WORDS_BIGENDIAN

	// check presence synchro code and shift trigger value         	
	start = (slen - inoffset) % slen;
	for (i=start; i<bs; i+=slen) {
		if (buf[i] != 0xFFFFFF00) {
			ci->report_error(&(a2dev->dev), EIO);
			return;
		}
		buf[i+1] >>= 8;
	}
	a2dev->inoffset = (inoffset + bs)%slen;

	// Update the eegdev structure with the new data
	ci->update_ringbuffer(&(a2dev->dev), buf, bs*sizeof(*buf));
}
                                                              	
                                                              	
#ifndef LIBUSB_CALL                                           	
#define LIBUSB_CALL                                           	
#endif                                                        	
static void LIBUSB_CALL req_completion_fn(struct libusb_transfer *transfer)
{                                                                     
	int ret;
	struct act2_eegdev* a2dev = transfer->user_data;
	const struct core_interface* ci = &(a2dev->dev.ci);

	// interpret the USB buffer content and update the ringbuffer
	if (transfer->actual_length)
		process_usbbuf(a2dev, (uint32_t*)transfer->buffer,
		               transfer->actual_length/sizeof(uint32_t));

	// Check that no error occured
	if ((ret = proc_libusb_transfer_ret(transfer->status))) {
		ci->report_error(&(a2dev->dev), ret);
		return;
	}
	
	// requeue again the chunk buffer if still running
	pthread_mutex_lock(&a2dev->mtx);
	if (a2dev->resubmit) {
		// Try submit
		if ((ret = libusb_submit_transfer(transfer))) {
			ci->report_error(&(a2dev->dev),
			                 proc_libusb_error(ret));
			a2dev->num_running--;
		}
	} else 	if (!--(a2dev->num_running))
		pthread_cond_signal(&a2dev->cond);
	pthread_mutex_unlock(&a2dev->mtx);
}


static int act2_disable_handshake(struct act2_eegdev* a2dev)
{
	int i;
	unsigned char usb_data[64] = {0};
	
	// Notify the Active2 hardware to stop acquiring data
	usb_data[0] = 0x00;
	act2_write(a2dev->hudev, usb_data, 64);

	// Notify urb to cancel and wait for them to actually finish
	pthread_mutex_lock(&a2dev->mtx);
	a2dev->resubmit = 0;
	for (i=0; i<NUMURB; i++)
		libusb_cancel_transfer(a2dev->urb[i]);
	
	while (a2dev->num_running)
		pthread_cond_wait(&a2dev->cond, &a2dev->mtx);
	pthread_mutex_unlock(&a2dev->mtx);

	return 0;
}



static int act2_enable_handshake(struct act2_eegdev* a2dev)
{
	unsigned char usb_data[64] = {0};
	uint32_t* buf = (uint32_t*) a2dev->urb[0]->buffer;
	int transferred, ret, i;

	// Init activetwo USB comm
	usb_data[0] = 0x00;
	act2_write(a2dev->hudev, usb_data, 64);

	// Start handshake
	usb_data[0] = 0xFF;
	act2_write(a2dev->hudev, usb_data, 64);

	// Transfer the first chunk of data and check the first synchro
	ret = libusb_bulk_transfer(a2dev->hudev, ACT2_EP_IN, 
	                           (unsigned char*)buf, CHUNKSIZE,
		                   &transferred, ACT2_TIMEOUT);
	ret = proc_libusb_error(ret);
	if (ret || (transferred < 2)
	   || (le_to_cpu_u32(buf[0])!=0xFFFFFF00)) {
		errno = ret ? ret : EIO;
		return -1;
	}

	// Parse the first trigger to get info about the system and transfer
	// the buffer into the ringbuffer
	parse_triggers(a2dev, le_to_cpu_u32(buf[1]));
	process_usbbuf(a2dev, buf, transferred/sizeof(*buf));

	// Submit all the URB in advance in order to queue them into the
	// USB host controller
	pthread_mutex_lock(&a2dev->mtx);
	a2dev->resubmit = 1;
	for (i=0; i<NUMURB; i++) {
		if ((ret = libusb_submit_transfer(a2dev->urb[i]))) {
			pthread_mutex_unlock(&a2dev->mtx);
			errno = proc_libusb_error(ret);
			act2_disable_handshake(a2dev);
			return -1;
		}
		a2dev->num_running++;
	}
	pthread_mutex_unlock(&a2dev->mtx);

	return 0;
}


static void* page_aligned_malloc(size_t len)
{
#if HAVE_POSIX_MEMALIGN && HAVE_SYSCONF
	int ret;
	void* memptr;
	size_t pgsize = sysconf(_SC_PAGESIZE);
	if ((ret = posix_memalign(&memptr, pgsize, len))) {
		errno = ret;
		return NULL;
	}
	else
		return memptr;
#else
	return malloc(len);
#endif
}


static int init_act2dev(struct act2_eegdev* a2dev, unsigned int nch)
{
 	int i;
	
	if (act2_open_dev(a2dev))
		return -1;
		
	// Initialize the asynchronous USB bulk transfer 
	for (i=0; i<NUMURB; i++) {
		if (!(a2dev->urb[i] = libusb_alloc_transfer(0))
		  ||!(a2dev->urb[i]->buffer=page_aligned_malloc(CHUNKSIZE)))
			goto error;

		libusb_fill_bulk_transfer(a2dev->urb[i],
		                          a2dev->hudev, ACT2_EP_IN,
		                          a2dev->urb[i]->buffer, CHUNKSIZE,
		                          req_completion_fn, a2dev,
		                          ACT2_TIMEOUT);
	}
	
	a2dev->optnch = nch;
	if (nch == 32)
		a2dev->eeglabel = eeg32label; 
	else if (nch == 64)
		a2dev->eeglabel = eeg64label;
	else
		a2dev->eeglabel = eeg256label;
	return 0;

error:
	for (i=0; i<NUMURB; i++) {
		if (a2dev->urb[i]) {
			free(a2dev->urb[i]->buffer);
			libusb_free_transfer(a2dev->urb[i]);
		}
	}
	act2_close_dev(a2dev);
	return -1; 
}


static void destroy_act2dev(struct act2_eegdev* a2dev)
{
	int i;

	if (a2dev == NULL)
		return;

	for (i=0; i<NUMURB; i++) {
		free(a2dev->urb[i]->buffer);
		libusb_free_transfer(a2dev->urb[i]);
	}
	act2_close_dev(a2dev);
}


/******************************************************************
 *               Activetwo methods implementation                 *
 ******************************************************************/
static
int act2_open_device(struct devmodule* dev, const char* optv[])
{
	unsigned int nch = atoi(optv[0]);
	struct act2_eegdev* a2dev = get_act2(dev);

	if (nch != 32 && nch != 64 && nch != 128 && nch != 256) {
		errno = EINVAL;
		return -1;
	}

	// alloc and initialize tructure
	if (init_act2dev(a2dev, nch))
		return -1;

	// Start the communication
	if (act2_enable_handshake(a2dev)) {
		destroy_act2dev(a2dev);
		return -1;
	}

	return 0;
}


static int act2_close_device(struct devmodule* dev)
{
	struct act2_eegdev* a2dev = get_act2(dev);
	
	act2_disable_handshake(a2dev);
	destroy_act2dev(a2dev);

	return 0;
}


static
int act2_set_channel_groups(struct devmodule* dev, unsigned int ngrp,
                            const struct grpconf* grp)
{
	unsigned int i, stype;
	struct selected_channels* sch;
	struct act2_eegdev* a2dev = get_act2(dev);
	
	if (!(sch = dev->ci.alloc_input_groups(dev, ngrp)))
		return -1;

	for (i=0; i<ngrp; i++) {
		stype = grp[i].sensortype;
		// Set parameters of (eeg -> ringbuffer)
		sch[i].in_offset = a2dev->offset[stype]
		                   + grp[i].index*sizeof(int32_t);
		sch[i].inlen = grp[i].nch*sizeof(int32_t);
		sch[i].bsc = (stype == EGD_TRIGGER) ? 0 : 1;
		sch[i].sc = act2_scales[grp[i].datatype];
		sch[i].typein = EGD_INT32;
		sch[i].typeout = grp[i].datatype;
		sch[i].iarray = grp[i].iarray;
		sch[i].arr_offset = grp[i].arr_offset;
	}
		
	return 0;
}


static void act2_fill_chinfo(const struct devmodule* dev, int stype,
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


API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct act2_eegdev),
	.open_device = 		act2_open_device,
	.close_device = 	act2_close_device,
	.set_channel_groups = 	act2_set_channel_groups,
	.fill_chinfo = 		act2_fill_chinfo,
	.supported_opts = 	act2_options
};


