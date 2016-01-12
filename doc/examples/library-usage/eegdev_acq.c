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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <eegdev.h>

#define NEEG		64
#define NSENS		8

#define NS_CHUNK	8
#define NS_TOTAL	500

struct systemcap {
	unsigned int sampling_freq;
	unsigned int eeg_nmax;
	unsigned int sensor_nmax;
	unsigned int trigger_nmax;
};


struct systemcap devcap;
struct grpconf grp[3] = {
	[0] = {
	       .index = 0, 
	       .iarray = 0, .datatype = EGD_FLOAT,
	       .arr_offset = 0, .nch = NEEG
	},
	[1] = {
	       .index = 0, 
	       .iarray = 0, .datatype = EGD_FLOAT,
	       .arr_offset = NEEG*sizeof(float), .nch = NSENS
	},
	[2] = {
	       .index = 0, 
	       .iarray = 1, .datatype = EGD_INT32,
	       .arr_offset = 0, .nch = 1
	}
};
size_t arrstrides[2];
int32_t* triggers = NULL;
float* analogch = NULL;


/* Adjust the groups and buffers size according to the capabilities of the
system and allocate the data buffers.
Returns 0 in case of success, -1 otherwise */
int setup_groups_buffers(void)
{
	const char* const types[3] = {"eeg", "undefined", "trigger"};
	int i;

	/* Get sensor type identifiers */
	for (i=0; i<3; i++)
		grp[i].sensortype = egd_sensor_type(types[i]);

	/* Adjust the groups configuration if the numbers of requested
	channels for EEG and sensor are too big */
	if (grp[0].nch > devcap.eeg_nmax)
		grp[0].nch = devcap.eeg_nmax;

	if (grp[1].nch > devcap.sensor_nmax)
		grp[1].nch = devcap.sensor_nmax;

	grp[1].arr_offset = devcap.eeg_nmax*sizeof(float);

	/* Setup the strides so that we get packed data into the buffers */
	arrstrides[0] = (grp[0].nch + grp[1].nch) * sizeof(float);
	arrstrides[1] = sizeof(int32_t);
	
	/* Allocate the buffers */
	analogch = malloc(arrstrides[0]*NS_CHUNK);
	triggers = malloc(arrstrides[1]*NS_CHUNK);
	if ((analogch == NULL) || (triggers == NULL))
		return -1;

	return 0;
}


void query_device_cap(struct eegdev* dev)
{
	const char *devmodel, *devid;

	egd_get_cap(dev, EGD_CAP_DEVTYPE, &devmodel);
	egd_get_cap(dev, EGD_CAP_DEVID, &devid);
	egd_get_cap(dev, EGD_CAP_FS, &devcap.sampling_freq);
	devcap.eeg_nmax = egd_get_numch(dev, egd_sensor_type("eeg"));
	devcap.trigger_nmax = egd_get_numch(dev, egd_sensor_type("trigger"));
	devcap.sensor_nmax = egd_get_numch(dev, egd_sensor_type("undefined"));


	printf("\tsampling frequency : %u Hz\n"
	       "\tModel type : %s\n"
	       "\tModel ID : %s\n"
	       "\tNumber of EEG channels : %u\n"
	       "\tNumber of sensor channels : %u\n"
	       "\tNumber of trigger channels : %u\n",
	       devcap.sampling_freq, 
	       devmodel, devid,
	       devcap.eeg_nmax,
	       devcap.sensor_nmax,
	       devcap.trigger_nmax);
}


int run_acquisition_loop(struct eegdev* dev)
{
	int i,j, num_ch;
	ssize_t ns;

	num_ch = grp[0].nch + grp[1].nch;

	printf("Starting acquisition...\n");
	egd_start(dev);

	i = 0;
	while (i < 500) {
		/* Fill the buffers with the next NS_CHUNK samples */
		ns = egd_get_data(dev, NS_CHUNK, analogch, triggers);
		if (ns <= 0)
			return -1;
		
		/* Display the trigger value and the 3rd channel in each
		samples */
		for (j=0; j<ns; j++)
			printf("sample %04i - tri: 0x%08x ch3: %f\n",
			       i+j, triggers[j], analogch[j*num_ch+2]);

		i += ns;
	}

	printf("Stopping acquisition...\n");
	egd_stop(dev);

	return 0;
}


int main(int argc, char* argv[])
{
	const char* devstring = NULL;
	struct eegdev* dev;
	int retcode = 1, opt;

	/* Process command line options */
	while ((opt = getopt(argc, argv, "e:s:d:h")) != -1) {
		if (opt == 'e')
			grp[0].nch = atoi(optarg);
		else if (opt == 's')
			grp[1].nch = atoi(optarg);
		else if (opt == 'd')
			devstring = optarg;
		else {
			fprintf(stderr, 
			        "Usage: %s [-e num_eeg_ch] "
				"[-s num_sensor_ch] [-d devstring]\n",
				argv[0]);
			return (opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
		}
	}

	/* Open the device with supplied device description
	If none is supplied (i.e. devstring == NULL), it tries to open
	any connected (and supported) device */
	dev = egd_open(devstring);
	if (dev == NULL) {
		fprintf(stderr, "Cannot open the device: %s\n", 
		        strerror(errno));
		return 1;
	}

	/* Get and display the capabilities of the system */
	query_device_cap(dev);

	/* Setup the acquisition transfer */
	printf("Setting up the acquisition...\n");
	if (setup_groups_buffers())
		goto exit;
	if (egd_acq_setup(dev, 2, arrstrides, 3, grp))
		goto exit;
	
	/* Start, run and stop the acquisition */
	if (run_acquisition_loop(dev))
		goto exit;

	retcode = 0;
exit:
	if (retcode)
		fprintf(stderr, "Error caught: %s\n", strerror(errno));

	/* Free data buffers */
	free(analogch);
	free(triggers);

	/* Close the connection to the system */
	egd_close(dev);

	return retcode;
}

