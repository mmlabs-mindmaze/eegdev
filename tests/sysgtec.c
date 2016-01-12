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
#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <eegdev.h>
#include "fakelibs/fakegtec.h"

#define DURATION	4	// in seconds
#define NSAMPLE	4
#define scaled_t	float

int verbose = 0;

static struct grpconf grp[3] = {
	{
		.index = 0,
		.iarray = 0,
		.arr_offset = 0,
		.nch = 16,
		.datatype = EGD_FLOAT
	},
	{
		.index = 0,
		.iarray = 0,
		.arr_offset = 16*sizeof(float),
		.nch = 0,
		.datatype = EGD_FLOAT
	},
	{
		.index = 0,
		.iarray = 1,
		.arr_offset = 0,
		.nch = 1,
		.datatype = EGD_INT32
	}
};

static
int check_signals_f(size_t ns, const float* sig, const int32_t* tri)
{
	size_t i=0;
	int nchtri = grp[2].nch;
	int nch = grp[0].nch + grp[1].nch;
	int ich;
	float expval;
	int32_t exptri;
	int retval = 0;

	static int checking = 0;
	static int nstot = 0, nsread = 0;


	// Identify the beginning of the expected sequence
	if (!checking) {
		for (i=0; i<ns; i++)
			if (tri[i*nchtri] == get_trigger_val(0, 0)) {
				checking = 1;
				break;
			}
		if (!checking && nsread > 113) {
			fprintf(stderr, "\tCannot find the beginning of the sequence\n");
			retval = -1;
		}
	}

	for (; i<ns; i++) {
		// Verify the values in the analog channels
		for (ich=0; ich<nch; ich++) {
			expval = get_analog_val(nstot, ich);
			if (sig[i*nch+ich] != expval) {
				fprintf(stderr, "\tSignal value (%f) different from the one expected (%f) at sample %zu ch:%u\n", sig[i*nch+ich], expval, i+nsread, ich);
				retval = -1;
			}
		}
		
		// Verify the values in the trigger channels
		for (ich=0; ich<nchtri; ich++) {
			exptri = get_trigger_val(nstot, ich);
			if (tri[i*nchtri+ich] != exptri) {
				fprintf(stderr, "\tTrigger value (%i) different from the one expected (%i) at sample %zu ch:%u\n", tri[i*nchtri+ich], exptri, i+nsread, ich);
				retval = -1;
			}
		}
		nstot++;
	}


	nsread += ns;
	return retval;
}


int check_signals_d(size_t ns, const double* sig, const int32_t* tri)
{
	size_t i=0;
	int nchtri = grp[2].nch;
	int nch = grp[0].nch + grp[1].nch;
	int ich;
	double expval;
	int32_t exptri;
	int retval = 0;

	static int checking = 0;
	static int nstot = 0, nsread = 0;


	// Identify the beginning of the expected sequence
	if (!checking) {
		for (i=0; i<ns; i++)
			if (tri[i*nchtri] == get_trigger_val(0, 0)) {
				checking = 1;
				break;
			}
		if (!checking && nsread > 113) {
			fprintf(stderr, "\tCannot find the beginning of the sequence\n");
			retval = -1;
		}
	}

	for (; i<ns; i++) {
		// Verify the values in the analog channels
		for (ich=0; ich<nch; ich++) {
			expval = get_analog_val(nstot, ich);
			if (sig[i*nch+ich] != expval) {
				fprintf(stderr, "\tSignal value (%f) different from the one expected (%f) at sample %zu ch:%u\n", sig[i*nch+ich], expval, i+nsread, ich);
				retval = -1;
			}
		}
		
		// Verify the values in the trigger channels
		for (ich=0; ich<nchtri; ich++) {
			exptri = get_trigger_val(nstot, ich);
			if (tri[i*nchtri+ich] != exptri) {
				fprintf(stderr, "\tTrigger value (%i) different from the one expected (%i) at sample %zu ch:%u\n", tri[i*nchtri+ich], exptri, i+nsread, ich);
				retval = -1;
			}
		}
		nstot++;
	}


	nsread += ns;
	return retval;
}


static
int simple_trigger_check(int is, int ns, int ntri, const int32_t* tri_t)
{
	static int32_t triref[8];
	int j, k, retcode = 0;
	
	if (is == 0) 
		memcpy(triref, tri_t, ntri*sizeof(*tri_t));

	for (j=0; j<ns; j++) {
		for (k=0; k<ntri; k++) {
			if (tri_t[ntri*j+k] != triref[k])  {
				fprintf(stderr, "\ttrigger value (0x%08x) "
				  "different from the one expected (0x%08x)"
				  " at sample %i channel %i\n",
				  tri_t[ntri*j+k], triref[k], is+j, k);
				triref[k] = tri_t[ntri*j+k];
				retcode = -1;
			}
		}
	}

	return retcode;
}


static
int print_cap(struct eegdev* dev, int* fs, int bsigcheck)
{
	unsigned int sampling_freq, eeg_nmax, sensor_nmax, trigger_nmax;
	char *device_type, *device_id;
	char prefiltering[128];
	int retval = 0;

	egd_get_cap(dev, EGD_CAP_DEVTYPE, &device_type);
	egd_get_cap(dev, EGD_CAP_DEVID, &device_id);
	egd_get_cap(dev, EGD_CAP_FS, &sampling_freq);
	eeg_nmax = egd_get_numch(dev, egd_sensor_type("eeg"));
	sensor_nmax = egd_get_numch(dev, egd_sensor_type("undefined"));
	trigger_nmax = egd_get_numch(dev, egd_sensor_type("trigger"));
	egd_channel_info(dev, egd_sensor_type("eeg"), 0,
				EGD_PREFILTERING, prefiltering, EGD_EOL);
	*fs = sampling_freq;

	if (bsigcheck) {
		retval = (sampling_freq == 1200) ? 0 : -1;
		if (retval)
			fprintf(stderr, "Unexpected option value\n");
	}

	if (!verbose)
		return retval;
	
	printf("\tVersion : %s\n"
	       "\tsystem capabilities:\n"
	       "\t\tdevice type: %s\n"
	       "\t\tdevice model: %s\n"
	       "\t\tsampling frequency: %u Hz\n"
	       "\t\tnum EEG channels: %u\n"
	       "\t\tnum sensor channels: %u\n"
	       "\t\tnum trigger channels: %u\n"
	       "\t\tprefiltering: %s\n",
	       egd_get_string(),
	       device_type, device_id, sampling_freq,
	       eeg_nmax, sensor_nmax, trigger_nmax, prefiltering);

	return retval;
}

static
int test_chinfo(struct eegdev* dev)
{
	unsigned int i, eegnch, sensnch, trinch;
	int isint;
	double dmm[2];
	int32_t imm[2];

	eegnch = egd_get_numch(dev, egd_sensor_type("eeg"));
	sensnch = egd_get_numch(dev, egd_sensor_type("undefined"));
	trinch = egd_get_numch(dev, egd_sensor_type("trigger"));

	for (i=0; i<eegnch; i++) {
		if (egd_channel_info(dev, egd_sensor_type("eeg"), i, EGD_MM_D, dmm, 
		                                EGD_ISINT, &isint, EGD_EOL))
			return -1;
		if (isint  || dmm[0] != -262144.0 
		           || dmm[1] != 262143.96875)
		  	return -1;
	}
	for (i=0; i<sensnch; i++) {
		if (egd_channel_info(dev, egd_sensor_type("undefined"), i, EGD_MM_D, dmm, 
		                                EGD_ISINT, &isint, EGD_EOL))
			return -1;
		if (isint  || dmm[0] != -262144.0 
		           || dmm[1] != 262143.96875)
		  	return -1;
	}
	for (i=0; i<trinch; i++) {
		if (egd_channel_info(dev, egd_sensor_type("trigger"), i, EGD_MM_I, imm, 
		                                EGD_ISINT, &isint, EGD_EOL))
			return -1;
		if (!isint  || imm[0] != -8388608 
		            || imm[1] != 8388607)
		  	return -1;
	}

	return 0;
}


static
struct eegdev* open_device(unsigned int nsystem, struct grpconf group[3])
{
	struct eegdev* dev;
	unsigned int i;
	int type = grp[0].datatype;
	size_t tsize = (type == EGD_FLOAT ? sizeof(float) : sizeof(double));
	char devicestr[256] = "device=gtec\ndeviceid=any";

	for (i=1; i<nsystem; i++)
		strcat(devicestr, "+any");

	if (!(dev = egd_open(devicestr)))
		return NULL;

	group[0].sensortype = egd_sensor_type("eeg");
	group[0].nch = egd_get_numch(dev, egd_sensor_type("eeg"));
	group[1].sensortype = egd_sensor_type("undefined");
	group[1].arr_offset = group[0].nch * tsize;
	group[1].nch = egd_get_numch(dev, egd_sensor_type("undefined"));
	group[2].sensortype = egd_sensor_type("trigger");
	group[2].nch = egd_get_numch(dev, egd_sensor_type("trigger"));

	return dev;
}


static
int read_eegsignal(unsigned int nsystem, int bsigcheck)
{
	struct eegdev* dev;
	int type = grp[0].datatype;
	size_t strides[2];
	void *eeg_t = NULL;
	int32_t *tri_t = NULL;
	int ntri, fs, i, baddata, retcode = 1;
	size_t tsize = (type == EGD_FLOAT ? sizeof(float) : sizeof(double));

	if (!(dev = open_device(nsystem, grp)))
		goto exit;

	// Get number of channels and configure structures
	strides[0] = (grp[0].nch + grp[1].nch)*tsize;
	strides[1] = grp[2].nch*sizeof(int32_t);
	ntri = grp[2].nch;

	eeg_t = calloc(strides[0], NSAMPLE);
	tri_t = calloc(strides[1], NSAMPLE);

	if (print_cap(dev, &fs, bsigcheck)) {
		retcode = 2;
		goto exit;
	}
	
	if (test_chinfo(dev)) {
		fprintf(stderr, "\tTest_chinfo failed\n");
		goto exit;
	}

	if (egd_acq_setup(dev, 2, strides, 3, grp))
	    	goto exit;

	if (egd_start(dev))
		goto exit;
	
	for (i=0; i < fs*DURATION; i += NSAMPLE) {
		if (egd_get_data(dev, NSAMPLE, eeg_t, tri_t) < 0) {
			fprintf(stderr, "\tAcq failed at sample %i\n",i);
			goto exit;
		}

		// No checking
		if (!bsigcheck) {
			if (simple_trigger_check(i, NSAMPLE, ntri, tri_t))
				retcode = 2;
			continue;
		}

		if (type == EGD_FLOAT)
			baddata = check_signals_f(NSAMPLE, eeg_t, tri_t);
		else
			baddata = check_signals_d(NSAMPLE, eeg_t, tri_t);

		if (baddata)
			retcode = 2;
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
	int retcode = 0, opt;
	unsigned int nsystem = 1;
	int bsigcheck = 0, usedouble = 0;

	while ((opt = getopt(argc, argv, "n:c:d:v:")) != -1) {
		switch (opt) {
		case 'n':
			nsystem = atoi(optarg);
			break;

		case 'c':
			bsigcheck = atoi(optarg);
			break;

		case 'd':
			usedouble = atoi(optarg);
			break;

		case 'v':
			verbose = atoi(optarg);
			break;

		default:	/* '?' */
			fprintf(stderr, "Usage: %s [-n numsystem] "
			                "[-c checking_expected_signals] "
					"[-d use_double] [-v verbosity]\n",
				argv[0]);
			return EXIT_FAILURE;
		}
	}

	printf("\tTesting gtec with %u system(s) with %s data type\n",
		nsystem, usedouble ? "double" : "float");

	if (usedouble)
		grp[0].datatype = grp[1].datatype = EGD_DOUBLE;

	// Test generation of a file
	retcode = read_eegsignal(nsystem, bsigcheck);


	return retcode;
}


