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
//#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <eegdev.h>

#define DURATION	4	// in seconds
#define NSAMPLE	4
#define NEEG	16
#define NEXG	0
#define NTRI	1
#define scaled_t	float

struct grpconf grp[3] = {
	{
		.sensortype = EGD_EEG,
		.index = 0,
		.iarray = 0,
		.arr_offset = 0,
		.nch = NEEG,
		.datatype = EGD_FLOAT
	},
	{
		.sensortype = EGD_SENSOR,
		.index = 0,
		.iarray = 0,
		.arr_offset = NEEG*sizeof(float),
		.nch = NEXG,
		.datatype = EGD_FLOAT
	},
	{
		.sensortype = EGD_TRIGGER,
		.index = 0,
		.iarray = 1,
		.arr_offset = 0,
		.nch = NTRI,
		.datatype = EGD_INT32
	}
};


static
int print_cap(struct eegdev* dev)
{
	unsigned int sampling_freq, eeg_nmax, sensor_nmax, trigger_nmax;
	char *device_type, *device_id;
	char prefiltering[128];

	egd_get_cap(dev, EGD_CAP_DEVTYPE, &device_type);
	egd_get_cap(dev, EGD_CAP_DEVID, &device_id);
	egd_get_cap(dev, EGD_CAP_FS, &sampling_freq);
	eeg_nmax = egd_get_numch(dev, EGD_EEG);
	sensor_nmax = egd_get_numch(dev, EGD_SENSOR);
	trigger_nmax = egd_get_numch(dev, EGD_TRIGGER);
	egd_channel_info(dev, EGD_EEG, 0, 
	                 EGD_PREFILTERING, prefiltering, EGD_EOL);
	
	printf("\tsystem capabilities:\n"
	       "\t\tdevice type: %s\n"
	       "\t\tdevice model: %s\n"
	       "\t\tsampling frequency: %u Hz\n"
	       "\t\tnum EEG channels: %u\n"
	       "\t\tnum sensor channels: %u\n"
	       "\t\tnum trigger channels: %u\n"
	       "\t\tprefiltering: %s\n",
	       device_type, device_id, sampling_freq,
	       eeg_nmax, sensor_nmax, trigger_nmax, prefiltering);

	return (int)sampling_freq;
}


int test_chinfo(struct eegdev* dev)
{
	unsigned int i, eegnch, sensnch, trinch;
	int isint;
	double dmm[2];
	int32_t imm[2];

	eegnch = egd_get_numch(dev, EGD_EEG);
	sensnch = egd_get_numch(dev, EGD_SENSOR);
	trinch = egd_get_numch(dev, EGD_TRIGGER);

	for (i=0; i<eegnch; i++) {
		if (egd_channel_info(dev, EGD_EEG, i, EGD_MM_D, dmm, 
		                                EGD_ISINT, &isint, EGD_EOL))
			return -1;
		if (isint  || dmm[0] != -262144.0 
		           || dmm[1] != 262143.96875)
		  	return -1;
	}
	for (i=0; i<sensnch; i++) {
		if (egd_channel_info(dev, EGD_SENSOR, i, EGD_MM_D, dmm, 
		                                EGD_ISINT, &isint, EGD_EOL))
			return -1;
		if (isint  || dmm[0] != -262144.0 
		           || dmm[1] != 262143.96875)
		  	return -1;
	}
	for (i=0; i<trinch; i++) {
		if (egd_channel_info(dev, EGD_TRIGGER, i, EGD_MM_I, imm, 
		                                EGD_ISINT, &isint, EGD_EOL))
			return -1;
		if (!isint  || imm[0] != -8388608 
		            || imm[1] != 8388607)
		  	return -1;
	}

	return 0;
}


int read_eegsignal(void)
{
	struct eegdev* dev;
	size_t strides[2] = {
		NEEG*sizeof(scaled_t)+NEXG*sizeof(scaled_t),
		NTRI*sizeof(int32_t)
	};
	scaled_t *eeg_t;
	int32_t *tri_t, triref;
	int i, j, retcode = 1;
	int fs;

	eeg_t = calloc(NSAMPLE*(NEEG+NEXG),sizeof(*eeg_t));
	tri_t = calloc(NSAMPLE*NTRI,sizeof(*tri_t));

	if ( !(dev = egd_open("gtec")) )
		goto exit;

	fs = print_cap(dev);

	
	if (test_chinfo(dev)) {
		fprintf(stderr, "\tTest_chinfo failed\n");
		goto exit;
	}

	if (egd_acq_setup(dev, 2, strides, 3, grp))
	    	goto exit;

	if (egd_start(dev))
		goto exit;
	
	i = 0;
	while (i < fs*DURATION) {
		if (egd_get_data(dev, NSAMPLE, eeg_t, tri_t) < 0) {
			fprintf(stderr, "\tAcq failed at sample %i\n",i);
			goto exit;
		}

		if (i == 0)
			triref = tri_t[0];

		for (j=0; j<NSAMPLE; j++)
			if (tri_t[j] != triref)  {
				fprintf(stderr, "\ttrigger value (0x%08x) different from the one expected (0x%08x) at sample %i\n", tri_t[j], triref, i+j);
				triref = tri_t[j];
				retcode = 2;
			}
		i += NSAMPLE;
	}

	if (egd_stop(dev))
		goto exit;

	if (egd_close(dev))
		goto exit;
	dev = NULL;

	if (retcode == 1)
		retcode = 0;
exit:
	if (retcode == 1)
		perror("\terror caught");

	egd_close(dev);
	free(eeg_t);
	free(tri_t);

	return retcode;
}


int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	int retcode = 0;

	fprintf(stderr, "\tTesting gtec\n\tVersion : %s\n", egd_get_string());

	// Test generation of a file
	retcode = read_eegsignal();


	return retcode;
}



