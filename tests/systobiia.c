/*
    Copyright (C) 2010-2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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

#include "fakelibs/tia-server.h"

#define DURATION	5	// in seconds
#define NSAMPLE	4
#define NEEG	7
#define NEXG	4
#define NTRI	1
#define PORT	38500
#define scaled_t	float

static char devhost[256] = "localhost";
static int verbose = 0;

static struct grpconf grp[3] = {
	{
		.index = 0,
		.iarray = 0,
		.arr_offset = 0,
		.nch = 64,
		.datatype = EGD_FLOAT
	},
	{
		.index = 0,
		.iarray = 1,
		.arr_offset = 0,
		.nch = 0,
		.datatype = EGD_FLOAT
	},
	{
		.index = 0,
		.iarray = 2,
		.arr_offset = 0,
		.nch = 1,
		.datatype = EGD_INT32
	}
};


int check_signals_f(size_t ns, const float* sig, const float* exg, const int32_t* tri)
{
	size_t i=0;
	int neeg = grp[0].nch;
	int nexg = grp[1].nch;
	int ntri = grp[2].nch;
	int ich;
	float expval;
	int32_t exptri;
	int retval = 0;

	static int nstot = 0, nsread = 0;


	// Identify the beginning of the expected sequence
	for (; i<ns && !retval; i++) {
		// Verify the values in the analog channels
		for (ich=0; ich<neeg && !retval; ich++) {
			expval = get_analog_val(nstot, ich);
			if (sig[i*neeg+ich] != expval) {
				fprintf(stderr, "\tEEG value (%f) different from the one expected (%f) at sample %zu ch:%u\n", sig[i*neeg+ich], expval, i+nsread, ich);
				retval = -1;
			}
		}

		// Verify the values in the exg channels
		for (ich=0; ich<nexg && !retval; ich++) {
			expval = get_analog_val(nstot, ich);
			if (exg[i*nexg+ich] != expval) {
				fprintf(stderr, "\tEXG value (%f) different from the one expected (%f) at sample %zu ch:%u\n", exg[i*nexg+ich], expval, i+nsread, ich);
				retval = -1;
			}
		}
		
		// Verify the values in the trigger channels
		for (ich=0; ich<ntri && !retval; ich++) {
			exptri = get_trigger_val(nstot, ich);
			if (tri[i*ntri+ich] != exptri) {
				fprintf(stderr, "\tTrigger value (0x%08x) different from the one expected (0x%08x) at sample %zu ch:%u\n", tri[i], exptri, i+nsread, ich);
				retval = -1;
			}
		}
		nstot++;
	}


	nsread += ns;
	return retval;
}


static
struct eegdev* open_device(struct grpconf group[3])
{
	struct eegdev* dev;
	int i;
	char devstring[256];
	const char* const sname[3] = {"eeg", "undefined", "trigger"};

	sprintf(devstring, "tobiia|host|%s|port|%i", devhost, PORT);
	if (!(dev = egd_open(devstring)))
		return NULL;

	for (i=0; i<3; i++) {
		group[i].sensortype = egd_sensor_type(sname[i]);
		group[i].nch = egd_get_numch(dev, group[i].sensortype);
	}

	return dev;
}


static
int print_cap(struct eegdev* dev)
{
	unsigned int sampling_freq, eeg_nmax, sensor_nmax, trigger_nmax;
	char *device_type, *device_id;
	int retval;

	egd_get_cap(dev, EGD_CAP_DEVTYPE, &device_type);
	egd_get_cap(dev, EGD_CAP_DEVTYPE, &device_id);
	egd_get_cap(dev, EGD_CAP_FS, &sampling_freq);
	eeg_nmax = egd_get_numch(dev, egd_sensor_type("eeg"));
	sensor_nmax = egd_get_numch(dev, egd_sensor_type("undefined"));
	trigger_nmax = egd_get_numch(dev, egd_sensor_type("trigger"));
	retval = (int)sampling_freq;
	
	if (!verbose)
		return retval;

	printf("\tsystem capabilities:\n"
	       "\t\tdevice type: %s\n"
	       "\t\tdevice model: %s\n"
	       "\t\tsampling frequency: %u Hz\n"
	       "\t\tnum EEG channels: %u\n"
	       "\t\tnum sensor channels: %u\n"
	       "\t\tnum trigger channels: %u\n",
	       device_type, device_type,
	       sampling_freq, eeg_nmax, sensor_nmax, trigger_nmax);

	return retval;
}


int read_eegsignal(int bsigcheck)
{
	struct eegdev* dev;
	size_t strides[3];
	size_t tsize = sizeof(scaled_t);
	void *eeg_t = NULL, *exg_t = NULL;
	int32_t *tri_t = NULL;
	int i, fs, retcode = 1;

	if (!(dev = open_device(grp)))
		goto exit;

	// Get number of channels and configure structures
	strides[0] = grp[0].nch*tsize;
	strides[1] = grp[1].nch*tsize;
	strides[2] = grp[2].nch*sizeof(int32_t);

	eeg_t = calloc(strides[0], NSAMPLE);
	exg_t = calloc(strides[1], NSAMPLE);
	tri_t = calloc(strides[2], NSAMPLE);


	fs = print_cap(dev);
	

	if (egd_acq_setup(dev, 3, strides, 3, grp))
	    	goto exit;

	if (egd_start(dev))
		goto exit;
	
	for (i=0; i < fs*DURATION; i += NSAMPLE) {
		if (egd_get_data(dev, NSAMPLE, eeg_t, exg_t, tri_t) < 0) {
			fprintf(stderr, "\tAcq failed at sample %i\n",i);
			goto exit;
		}

		if (bsigcheck
		   && check_signals_f(NSAMPLE, eeg_t, exg_t, tri_t)) {
			retcode = 2;
			break;
		}
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
	free(exg_t);
	free(tri_t);

	return retcode;
}


int main(int argc, char *argv[])
{
	int opt, retcode = 0, bsigcheck = 0;

	while ((opt = getopt(argc, argv, "c:v:s:")) != -1) {
		switch (opt) {
		case 'c':
			bsigcheck = atoi(optarg);
			break;

		case 's':
			strcpy(devhost, optarg);
			break;

		case 'v':
			verbose = atoi(optarg);
			break;

		default:	/* '?' */
			fprintf(stderr, "Usage: %s "
			                "[-c checking_expected_signals] "
					"[-s server] [-v verbosity]\n",
				argv[0]);
			return EXIT_FAILURE;
		}
	}
	printf("\tTesting tobiia\n");

	if (bsigcheck)
		create_tia_server(PORT);

	retcode = read_eegsignal(bsigcheck);

	if (bsigcheck)
		destroy_tia_server();

	return retcode;
}



