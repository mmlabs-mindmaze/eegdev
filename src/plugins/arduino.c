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
#include <termios.h>
#include <sys/time.h>

#include <eegdev-pluginapi.h>

struct arduino_eegdev {
	struct devmodule dev;
	struct termios tty;
	pthread_t thread_id;
	int pid;
	float refmv;
	pthread_mutex_t acqlock;
	unsigned int runacq; 
	char* pname;
};

#define get_arduino(dev_p) ((struct arduino_eegdev*)(dev_p))

#define NCH 	1

static const char arduinolabel[NCH][8] = {
	"EMG1"
};
static const char arduinounit[] = "mV";
static const char arduinotransducter[] = "EMG electrode";
	
static const union gval arduino_scales[EGD_NUM_DTYPE] = {
	[EGD_INT32] = {.valint32_t = 1},
	[EGD_FLOAT] = {.valfloat = 1.0f},	// in uV
	[EGD_DOUBLE] = {.valdouble = 1.0}	// in uV
};
static const int arduino_provided_stypes[] = {EGD_SENSOR};

enum {OPT_PORT, OPT_REFMV,NUMOPT};
static const struct egdi_optname arduino_options[] = {
	[OPT_PORT] = {.name = "port", .defvalue = "/dev/ttyACM0"},
	[OPT_REFMV] = {.name = "refmv", .defvalue = "5000"},
	{.name = NULL}
};

static void* arduino_read_fn(void* arg)
{
	struct arduino_eegdev* arduinodev = arg;
	const struct core_interface* restrict ci = &arduinodev->dev.ci;
	int runacq;

	fprintf(stdout,"REF = %f\n",arduinodev->refmv);

	/* Allocate memory for read buffer */
	char header;
	char lowByte;
	char highByte;
	float* fsample = (float*)malloc(1*sizeof(float));

        struct timeval start, end;
	while (1) {
		pthread_mutex_lock(&(arduinodev->acqlock));
		runacq = arduinodev->runacq;
		pthread_mutex_unlock(&(arduinodev->acqlock));
		if (!runacq)
			break;

		/* *** READ *** */	        
		gettimeofday(&start,NULL);
		// Read 1-byte header
		int n = read(arduinodev->pid, &header , 1);
		if(n!=1)
		    fprintf(stdout,"Error reading: %s\n",strerror(errno));
		if(header != '\t')
		    continue;
		// Read highByte
 		n = read(arduinodev->pid, &highByte , 1);
		if(n!=1)
		    fprintf(stdout,"Error reading: %s\n",strerror(errno));
		// Read lowhByte
		n = read(arduinodev->pid,  &lowByte , 1);
		if(n!=1)
		    fprintf(stdout,"Error reading: %s\n",strerror(errno));
                gettimeofday(&end,NULL);
   	        float ElapsedTime = (float)1000*(end.tv_sec-start.tv_sec)+(end.tv_usec-start.tv_usec)/1000;
	        fprintf(stdout,"One frame/sample in %f milliseconds\n", ElapsedTime);
		
		// Convert the two measurement bytes to int
		int number = lowByte | highByte << 8;
		//Convert int to float applying the scaling
		fsample[0] = ( arduinodev->refmv)*((float)(number)/1023.0f); //in mV
		fprintf(stdout,"%f\n",1000.0*fsample[0]);
		
		// Update the eegdev structure with the new data
		if (ci->update_ringbuffer(&(arduinodev->dev), fsample, NCH*sizeof(float)))
			break;
		
	}
	return NULL;
error:
	ci->report_error(&(arduinodev->dev), EIO);
	return NULL;
}


static
int arduino_set_capability(struct arduino_eegdev* arduinodev)
{
	struct systemcap cap = {
		.sampling_freq = 256, 
		.type_nch = {[EGD_SENSOR] = NCH},
		.device_type = "Arduino",
		.device_id = "Arduino"
	};
	struct devmodule* dev = &(arduinodev->dev);

	dev->ci.set_cap(dev, &cap);
	dev->ci.set_input_samlen(dev, NCH*sizeof(int32_t));
	return 0;
}


/******************************************************************
 *               Arduino methods implementation                	  *
 ******************************************************************/
static
int arduino_open_device(struct devmodule* dev, const char* optv[])
{

	struct arduino_eegdev* arduinodev = get_arduino(dev);
	
	// Get reference value from argument
	arduinodev->refmv = (float)atoi(optv[OPT_REFMV]);	

	//Open the serial port
	arduinodev->pname = optv[OPT_PORT];
	//arduinodev->pid = open(arduinodev->pname, O_RDWR | O_NOCTTY | O_SYNC);
	arduinodev->pid = open(arduinodev->pname, O_RDWR | O_NOCTTY);

	if (arduinodev->pid < 0)
	{
		fprintf(stdout, "Error opening serial %s port!\n",arduinodev->pname);
		exit(EXIT_FAILURE);
	}
	fprintf(stdout, "Connected to port: %s\n",arduinodev->pname);

	memset(&(arduinodev->tty), 0, sizeof(arduinodev->tty));

	/* Error Handling */
	if ( tcgetattr ( arduinodev->pid, &(arduinodev->tty) ) != 0 ) {
	   fprintf(stdout,"Error %d from tcgetattr %s:\n",errno, strerror(errno));
	}

	/* Set Baud Rate */
	cfsetospeed(&(arduinodev->tty), (speed_t)B9600);
	cfsetispeed(&(arduinodev->tty), (speed_t)B9600);

	/* Setting other Port Stuff */
	arduinodev->tty.c_cflag     &=  ~PARENB;            // Make 8n1
	arduinodev->tty.c_cflag     &=  ~CSTOPB;
	arduinodev->tty.c_cflag     &=  ~CSIZE;
	arduinodev->tty.c_cflag     |=  CS8;

	arduinodev->tty.c_cflag     &=  ~CRTSCTS;           // no flow control
	//arduinodev->tty.c_cc[VMIN]   =  1;                  // read doesn't block
	arduinodev->tty.c_cc[VMIN]   =  0;                  // read blocks????
	arduinodev->tty.c_cc[VTIME]  =  2;                  // 0.5 seconds read timeout
	arduinodev->tty.c_cflag     |=  CREAD | CLOCAL;     // turn on READ & ignore ctrl lines

	/* Make raw */
	cfmakeraw(&(arduinodev->tty));

	/* Flush Port, then applies attributes */
	tcflush( arduinodev->pid, TCIFLUSH );
	if ( tcsetattr ( arduinodev->pid, TCSANOW, &(arduinodev->tty)) != 0) {
	   fprintf(stdout, "Error %d from tcsetattr\n",errno);
	}

	//Set capabilities	
	arduino_set_capability(arduinodev);
	

	pthread_mutex_init(&(arduinodev->acqlock), NULL);
	arduinodev->runacq = 1;

	if ((pthread_create(&(arduinodev->thread_id), NULL, 
	                           arduino_read_fn, arduinodev)))
		goto error;
	
	return 0;

error:
	return -1;
}


static
int arduino_close_device(struct devmodule* dev)
{
	struct arduino_eegdev* arduinodev = get_arduino(dev);

	pthread_mutex_lock(&(arduinodev->acqlock));
	arduinodev->runacq = 0;
	pthread_mutex_unlock(&(arduinodev->acqlock));

	pthread_join(arduinodev->thread_id, NULL);
	pthread_mutex_destroy(&(arduinodev->acqlock));
	
	//Close the serial port here
	close(arduinodev->pid);	
	return 0;
}


static
int arduino_set_channel_groups(struct devmodule* dev, unsigned int ngrp,
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
		selch[i].bsc = 0;
		selch[i].typein = EGD_FLOAT;
		selch[i].sc = arduino_scales[grp[i].datatype];
		selch[i].typeout = grp[i].datatype;
		selch[i].iarray = grp[i].iarray;
		selch[i].arr_offset = grp[i].arr_offset;
	}
		
	return 0;
}


static void arduino_fill_chinfo(const struct devmodule* dev, int stype,
	                     unsigned int ich, struct egd_chinfo* info)
{
	(void)dev;
	(void)stype;

	info->isint = 0;
	info->dtype = EGD_FLOAT;
	info->min.valdouble = -512.0 * arduino_scales[EGD_DOUBLE].valdouble;
	info->max.valdouble = 511.0 * arduino_scales[EGD_DOUBLE].valdouble;
	info->label = arduinolabel[ich];
	info->unit = arduinounit;
	info->transducter = arduinotransducter;
}


API_EXPORTED
const struct egdi_plugin_info eegdev_plugin_info = {
	.plugin_abi = 	EEGDEV_PLUGIN_ABI_VERSION,
	.struct_size = 	sizeof(struct arduino_eegdev),
	.open_device = 		arduino_open_device,
	.close_device = 	arduino_close_device,
	.set_channel_groups = 	arduino_set_channel_groups,
	.fill_chinfo = 		arduino_fill_chinfo,
	.supported_opts =	arduino_options
};

