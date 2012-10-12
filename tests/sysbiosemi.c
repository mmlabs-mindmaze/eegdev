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
#include "fakelibs/fakeact2.h"

#define DURATION	4	// in seconds
#define NSAMPLE	4
#define scaled_t	float

int verbose = 0;

static int checking = 0;
static int nstot = 0, nsread = 0;

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

static
int check_signals_f(size_t ns, const float* sig, const float* exg, const int32_t* tri)
{
	size_t i=0;
	int neeg = grp[0].nch;
	int nexg = grp[1].nch;
	int ich;
	float expval;
	int32_t exptri, stateval = tri[0] & 0x00FF0000;
	int retval = 0;

	// Identify the beginning of the expected sequence
	if (!checking) {
		for (i=0; i<ns; i++) {
			if (tri[i] == get_trigger_val(0, stateval)) {
				checking = 1;
				break;
			}
		}
		if (!checking && nsread > PERIOD) {
			fprintf(stderr, "\tCannot find the beginning of the sequence\n");
			retval = -1;
		}
	}

	for (; i<ns && !retval; i++) {
		// Verify the values in the analog channels
		for (ich=0; ich<neeg && !retval; ich++) {
			expval = get_analog_valf(nstot, ich, 0);
			if (sig[i*neeg+ich] != expval) {
				fprintf(stderr, "\tEEG value (%f) different from the one expected (%f) at sample %zu ch:%u\n", sig[i*neeg+ich], expval, i+nsread, ich);
				retval = -1;
			}
		}

		// Verify the values in the exg channels
		for (ich=0; ich<nexg && !retval; ich++) {
			expval = get_analog_valf(nstot, ich, 1);
			if (exg[i*nexg+ich] != expval) {
				fprintf(stderr, "\tEXG value (%f) different from the one expected (%f) at sample %zu ch:%u\n", exg[i*nexg+ich], expval, i+nsread, ich);
				retval = -1;
			}
		}
		
		// Verify the values in the trigger channels
		exptri = get_trigger_val(nstot, stateval);
		if (tri[i] != exptri) {
			fprintf(stderr, "\tTrigger value (0x%08x) different from the one expected (0x%08x) at sample %zu ch:%u\n", tri[i], exptri, i+nsread, ich);
			retval = -1;
		}
		nstot++;
	}


	nsread += ns;
	return retval;
}


int check_signals_d(size_t ns, const double* sig, const double* exg, const int32_t* tri)
{
	size_t i=0;
	int nchtri = grp[2].nch;
	int neeg = grp[0].nch;
	int nexg = grp[1].nch;
	int ich;
	double expval;
	int32_t exptri, stateval = tri[0] & 0x00FF0000;
	int retval = 0;


	// Identify the beginning of the expected sequence
	if (!checking) {
		for (i=0; i<ns; i++)
			if (tri[i] == get_trigger_val(0, stateval)) {
				checking = 1;
				break;
			}
		if (!checking && nsread > PERIOD) {
			fprintf(stderr, "\tCannot find the beginning of the sequence\n");
			retval = -1;
		}
	}

	for (; i<ns && !retval; i++) {
		// Verify the values in the analog channels
		for (ich=0; ich<neeg && !retval; ich++) {
			expval = get_analog_vald(nstot, ich, 0);
			if (sig[i*neeg+ich] != expval) {
				fprintf(stderr, "\tEEG value (%f) different from the one expected (%f) at sample %zu ch:%u\n", sig[i*neeg+ich], expval, i+nsread, ich);
				retval = -1;
			}
		}

		// Verify the values in the analog channels
		for (ich=0; ich<nexg && !retval; ich++) {
			expval = get_analog_vald(nstot, ich, 1);
			if (exg[i*nexg+ich] != expval) {
				fprintf(stderr, "\tEXG value (%f) different from the one expected (%f) at sample %zu ch:%u\n", exg[i*nexg+ich], expval, i+nsread, ich);
				retval = -1;
			}
		}
		
		// Verify the values in the trigger channels
		for (ich=0; ich<nchtri && !retval; ich++) {
			exptri = get_trigger_val(nstot, stateval);
			if (tri[i] != exptri) {
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
int print_cap(struct eegdev* dev)
{
	unsigned int sampling_freq, eeg_nmax, sensor_nmax, trigger_nmax;
	char *device_type, *device_id;
	char prefiltering[128];
	int retval;

	egd_get_cap(dev, EGD_CAP_DEVTYPE, &device_type);
	egd_get_cap(dev, EGD_CAP_DEVID, &device_id);
	egd_get_cap(dev, EGD_CAP_FS, &sampling_freq);
	eeg_nmax = egd_get_numch(dev, egd_sensor_type("eeg"));
	sensor_nmax = egd_get_numch(dev, egd_sensor_type("undefined"));
	trigger_nmax = egd_get_numch(dev, egd_sensor_type("trigger"));
	egd_channel_info(dev, egd_sensor_type("eeg"), 0,
				EGD_PREFILTERING, prefiltering, EGD_EOL);
	retval = (int)sampling_freq;

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
struct eegdev* open_device(struct grpconf group[3])
{
	struct eegdev* dev;
	char devicestr[256] = "biosemi";

	if (!(dev = egd_open(devicestr)))
		return NULL;

	group[0].nch = egd_get_numch(dev, egd_sensor_type("eeg"));
	group[1].nch = egd_get_numch(dev, egd_sensor_type("undefined"));
	group[2].nch = egd_get_numch(dev, egd_sensor_type("trigger"));

	return dev;
}


static
int read_eegsignal(int bsigcheck, int pass)
{
	struct eegdev* dev;
	int type = grp[0].datatype;
	size_t strides[3];
	void *eeg_t = NULL, *exg_t = NULL;
	int32_t *tri_t = NULL;
	int ntri, fs, i, baddata, retcode = 1;
	size_t tsize = (type == EGD_FLOAT ? sizeof(float) : sizeof(double));

	// Reset global variable used to track the expected signal
	checking = 0;
	nstot = nsread = 0;

	if (!(dev = open_device(grp)))
		goto exit;

	// Get number of channels and configure structures
	grp[0].sensortype = egd_sensor_type("eeg");
	grp[1].sensortype = egd_sensor_type("undefined");
	grp[2].sensortype = egd_sensor_type("trigger");
	strides[0] = grp[0].nch*tsize;
	strides[1] = grp[1].nch*tsize;
	strides[2] = grp[2].nch*sizeof(int32_t);
	ntri = grp[2].nch;

	eeg_t = calloc(strides[0], NSAMPLE);
	exg_t = calloc(strides[1], NSAMPLE);
	tri_t = calloc(strides[2], NSAMPLE);

	fs = print_cap(dev);

	
	if (test_chinfo(dev)) {
		fprintf(stderr, "\tTest_chinfo failed\n");
		goto exit;
	}

	if (egd_acq_setup(dev, 3, strides, 3, grp))
	    	goto exit;

	if (egd_start(dev))
		goto exit;
	
	for (i=0; i < fs*DURATION; i += NSAMPLE) {
		if (egd_get_data(dev, NSAMPLE, eeg_t, exg_t, tri_t) < 0) {
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
			baddata = check_signals_f(NSAMPLE, eeg_t, exg_t, tri_t);
		else
			baddata = check_signals_d(NSAMPLE, eeg_t, exg_t, tri_t);

		if (baddata) {
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
		fprintf(stderr, "\terror caught at pass %i: %s",
		                pass, strerror(errno));

	egd_close(dev);
	free(eeg_t);
	free(exg_t);
	free(tri_t);

	return retcode;
}


int main(int argc, char *argv[])
{
	int retcode = 0, opt;
	int bsigcheck = 0, usedouble = 0;
	int numpass = 2;

	int pass;

	while ((opt = getopt(argc, argv, "c:d:v:p:")) != -1) {
		switch (opt) {
		case 'c':
			bsigcheck = atoi(optarg);
			break;

		case 'd':
			usedouble = atoi(optarg);
			break;

		case 'v':
			verbose = atoi(optarg);
			break;

		case 'p':
			numpass = atoi(optarg);
			break;

		default:	/* '?' */
			fprintf(stderr, "Usage: %s "
			                "[-c checking_expected_signals] "
					"[-d use_double] [-v verbosity]\n",
				argv[0]);
			return EXIT_FAILURE;
		}
	}

	printf("\tTesting biosemi with %s data type\n",
			usedouble ? "double" : "float");

	if (usedouble)
		grp[0].datatype = grp[1].datatype = EGD_DOUBLE;

	// Test for a numpass times
	// More than one pass allows to test the cleanup code
	for (pass=0; pass<numpass && !retcode; pass++)
		retcode = read_eegsignal(bsigcheck, pass);

	return retcode;
}


