/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
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

#define DURATION	1	// in seconds
#define NSAMPLE	4
#define NEEG	7
#define NEXG	0
#define NTRI	0
#define scaled_t	float

char devpath[256] = "/dev/rfcomm0";

struct grpconf grp[1] = {
	{
		.sensortype = EGD_EEG,
		.index = 0,
		.iarray = 0,
		.arr_offset = 0,
		.nch = NEEG,
		.datatype = EGD_FLOAT
	},
};

static
int print_cap(struct eegdev* dev)
{
	unsigned int sampling_freq, eeg_nmax, sensor_nmax, trigger_nmax;
	char *device_type, *device_id;

	egd_get_cap(dev, EGD_CAP_DEVTYPE, &device_type);
	egd_get_cap(dev, EGD_CAP_DEVTYPE, &device_id);
	egd_get_cap(dev, EGD_CAP_FS, &sampling_freq);
	eeg_nmax = egd_get_numch(dev, EGD_EEG);
	sensor_nmax = egd_get_numch(dev, EGD_SENSOR);
	trigger_nmax = egd_get_numch(dev, EGD_TRIGGER);
	
	printf("\tsystem capabilities:\n"
	       "\t\tdevice type: %s\n"
	       "\t\tdevice model: %s\n"
	       "\t\tsampling frequency: %u Hz\n"
	       "\t\tnum EEG channels: %u\n"
	       "\t\tnum sensor channels: %u\n"
	       "\t\tnum trigger channels: %u\n",
	       device_type, device_type,
	       sampling_freq, eeg_nmax, sensor_nmax, trigger_nmax);

	return (int)sampling_freq;
}


int read_eegsignal(void)
{
	struct eegdev* dev;
	size_t strides[1] = {NEEG*sizeof(scaled_t)};
	scaled_t *eeg_t;
	int i, retcode = 1;
	int fs;
	char devstring[256];

	eeg_t = calloc(NSAMPLE*NEEG,sizeof(*eeg_t));

	sprintf(devstring, "neurosky|path|%s", devpath);
	if ( !(dev = egd_open(devstring)) )
		goto exit;

	fs = print_cap(dev);
	

	if (egd_acq_setup(dev, 1, strides, 1, grp))
	    	goto exit;

	if (egd_start(dev))
		goto exit;
	
	i = 0;
	while (i < fs*DURATION) {
		if (egd_get_data(dev, NSAMPLE, eeg_t) < 0) {
			fprintf(stderr, "\tAcq failed at sample %i\n",i);
			goto exit;
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
		fprintf(stderr, "\terror caught (%i) %s\n",errno,strerror(errno));

	egd_close(dev);
	free(eeg_t);

	return retcode;
}


int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	int retcode = 0;

	fprintf(stderr, "\tTesting neurosky\n\tVersion : %s\n", egd_get_string());

	if (argc >= 2)
		strcpy(devpath, argv[1]);
	// Test generation of a file
	retcode = read_eegsignal();


	return retcode;
}



