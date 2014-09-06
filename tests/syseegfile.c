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

//#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <xdfio.h>
#include <unistd.h>
#include <errno.h>
#include <eegdev.h>

#define SAMPLINGRATE	128	// in Hz
#define DURATION	2	// in seconds
#define NITERATION	((SAMPLINGRATE*DURATION)/NSAMPLE)
#define NSAMPLE	17
#define NEEG	11
#define NEXG	7
#define NTRI	1
#define NEEGT	(NEEG-4)
#define NEXGT	(NEXG-1)
#define NTRIT	NTRI
#define scaled_t	float
static const enum xdftype arrtype = XDFFLOAT;
static const enum xdftype sttype = XDFINT24;
static const enum xdftype trigsttype = XDFINT24;
static const enum xdftype trigarrtype = XDFINT32;
static const unsigned int grpindex[EGD_NUM_STYPE] = {
	[0] = 0,
	[1] = NEEG+NEXG,
	[2] = NEEG
};

int verbose = 0;

void write_signal(scaled_t* eegdata, scaled_t* exgdata, int32_t* tridata, int* currsample, unsigned int ns)
{
	unsigned int i, isample, j, seed = *currsample;

	for(i=0; i<ns; i++) {
		isample = i+seed;
		for (j=0; j<NEEG; j++)	
			eegdata[i*NEEG+j] = (i%23)/*((j+1)*i)+seed*/;
	
		for (j=0; j<NEXG; j++)	
			exgdata[i*NEXG+j] = (j+1)*(isample%ns);

		for (j=0; j<NTRI; j++)
			tridata[i*NTRI+j] = (i%10 == 0) ? 6 : 0;
	}
	*currsample += ns;
}

static int set_default_analog(struct xdf* xdf, int arrindex)
{
	xdf_set_conf(xdf, 
		XDF_CF_ARRTYPE, arrtype,
		XDF_CF_ARRINDEX, arrindex,
		XDF_CF_ARROFFSET, 0,
		XDF_CF_TRANSDUCTER, "Active Electrode",
		XDF_CF_PREFILTERING, "HP: DC; LP: 417 Hz",
		XDF_CF_PMIN, -262144.0,
		XDF_CF_PMAX, 262143.0,
		XDF_CF_UNIT, "uV",
		XDF_CF_RESERVED, "EEG",
		XDF_NOF);
	
	return 0;
}

static int set_default_trigger(struct xdf* xdf, int arrindex)
{
	xdf_set_conf(xdf, 
		XDF_CF_ARRTYPE, trigarrtype,
		XDF_CF_ARRINDEX, arrindex,
		XDF_CF_ARROFFSET, 0,
		XDF_CF_TRANSDUCTER, "Triggers and Status",
		XDF_CF_PREFILTERING, "No filtering",
		XDF_CF_PMIN, -8388608.0,
		XDF_CF_PMAX, 8388607.0,
		XDF_CF_UNIT, "Boolean",
		XDF_CF_RESERVED, "TRI",
		XDF_NOF);
	
	return 0;
}


int generate_bdffile(const char* filename)
{
	scaled_t* eegdata;
	scaled_t* exgdata;
	int32_t* tridata;
	int retval = -1;
	struct xdf* xdf = NULL;
	int i,j;
	char tmpstr[16];
	size_t strides[3] = {
		NEEG*sizeof(*eegdata),
		NEXG*sizeof(*exgdata),
		NTRI*sizeof(*tridata)
	};

	// Allocate the temporary buffers for samples
	eegdata = malloc(NEEG*NSAMPLE*sizeof(*eegdata));
	exgdata = malloc(NEXG*NSAMPLE*sizeof(*exgdata));
	tridata = malloc(NTRI*NSAMPLE*sizeof(*tridata));
	if (!eegdata || !exgdata || !tridata)
		goto exit;
		
	xdf = xdf_open(filename, XDF_WRITE, XDF_BDF);
	if (!xdf) 
		goto exit;
	
	// Specify the structure (channels and sampling rate)
	xdf_set_conf(xdf, XDF_F_SAMPLING_FREQ, (int)SAMPLINGRATE, XDF_NOF);
	set_default_analog(xdf, 0);
	for (j=0; j<NEEG; j++) {
		sprintf(tmpstr, "EEG%i", j);
		if (!xdf_add_channel(xdf, tmpstr))
			goto exit;
	}

	xdf_set_conf(xdf, XDF_CF_ARRINDEX, 1, XDF_CF_ARROFFSET, 0, XDF_NOF);
	for (j=0; j<NEXG; j++) {
		sprintf(tmpstr, "EXG%i", j);
		if (!xdf_add_channel(xdf, tmpstr))
			goto exit;
	}

	set_default_trigger(xdf, 2);
	for (j=0; j<NTRI; j++) {
		sprintf(tmpstr, "TRI%i", j);
		if (!xdf_add_channel(xdf, tmpstr))
			goto exit;
	}

	xdf_define_arrays(xdf, 3, strides);
	if (xdf_prepare_transfer(xdf) < 0)
		goto exit;
	
	// Feed with samples
	i = 0;
	while (i<NITERATION*NSAMPLE) {
		// Set data signals and unscaled them
		write_signal(eegdata, exgdata, tridata, &i, NSAMPLE);
		if (xdf_write(xdf, NSAMPLE, eegdata, exgdata, tridata) < 0)
			goto exit;
	}
	retval = 0;

exit:		
	// Clean the structures and ressources
	if (retval) {
		fprintf(stderr, 
		     "\tFailure while creating the file (%s) error: %s\n",
		     filename,
		     strerror(errno));
		exit(1);
	}
	xdf_close(xdf);
	free(eegdata);
	free(exgdata);
	free(tridata);


	return 0;
}


struct grpconf grp[3] = {
	{
		.index = 0,
		.iarray = 0,
		.arr_offset = 0,
		.nch = NEEGT,
		.datatype = EGD_FLOAT
	},
	{
		.index = 0,
		.iarray = 1,
		.arr_offset = 0,
		.nch = NEXGT,
		.datatype = EGD_FLOAT
	},
	{
		.index = 0,
		.iarray = 2,
		.arr_offset = 0,
		.nch = NTRIT,
		.datatype = EGD_INT32
	}
};

const char* const sname[] = {"eeg", "undefined", "trigger"};

struct xdf* setup_testfile(char genfilename[])
{
	int offset;
	unsigned int i, numch;
	struct xdf* xdf;
	struct xdfch* ch;
	size_t strides[3];
	
	xdf = xdf_open(genfilename, XDF_READ, XDF_BDF);
	if (!xdf)
		return NULL;

	// Not all channel will be sourced
	xdf_get_conf(xdf, XDF_F_NCHANNEL, &numch, XDF_NOF);
	for (i=0; i<numch; i++) {
		ch = xdf_get_channel(xdf, i);
		xdf_set_chconf(ch, XDF_CF_ARRINDEX, -1, XDF_NOF);
	}

	for (i=0; i<3; i++)
		grp[i].sensortype = egd_sensor_type(sname[i]);
	

	offset = 0;
	for (i=0; i<NEEGT; i++) {
		if (xdf_set_chconf(xdf_get_channel(xdf, i),
				XDF_CF_ARRDIGITAL, 0,
				XDF_CF_ARRINDEX, 0,
				XDF_CF_ARRTYPE, XDFFLOAT,
				XDF_CF_ARROFFSET, offset,
				XDF_NOF))
			goto error;
		offset += sizeof(float);
	}
	strides[0] = offset;

	offset = 0;
	for (i=0; i<NEXGT; i++) {
		if (xdf_set_chconf(xdf_get_channel(xdf, i+NEEG),
				XDF_CF_ARRDIGITAL, 0,
				XDF_CF_ARRINDEX, 1,
				XDF_CF_ARRTYPE, XDFFLOAT,
				XDF_CF_ARROFFSET, offset,
				XDF_NOF))
			goto error;
		offset += sizeof(float);
	}
	strides[1] = offset;

	offset = 0;
	for (i=0; i<NTRIT; i++) {
		if (xdf_set_chconf(xdf_get_channel(xdf, i+NEEG+NEXG),
				XDF_CF_ARRDIGITAL, 0,
				XDF_CF_ARRINDEX, 2,
				XDF_CF_ARRTYPE, XDFINT32,
				XDF_CF_ARROFFSET, offset,
				XDF_NOF))
			goto error;
		offset += sizeof(int32_t);
	}
	strides[2] = offset;

	if (xdf_define_arrays(xdf, 3, strides)
	    || xdf_prepare_transfer(xdf))
		goto error;

	return xdf;
error:
	xdf_close(xdf);
	return NULL;
}


int test_chinfo(struct eegdev* dev, struct xdf* xdf)
{
	unsigned int i, s;
	struct xdfch* ch;
	double rmin, rmax, tmm[2];
	char *rlabel, tlabel[64];
	char *runit, tunit[16];
	char *rtransducer, ttransducer[128];
	char *rprefiltering, tprefiltering[128];
	unsigned int nch[3] = {
		[0] = NEEG,
		[1] = NTRI,
		[2] = NEXG
	};

	for (s = 0; s<3; s++) {
		for (i=0; i<nch[s]; i++) {
			ch=xdf_get_channel(xdf, i+grpindex[s]);
			xdf_get_chconf(ch, XDF_CF_LABEL, &rlabel,
			            XDF_CF_PMIN, &rmin, XDF_CF_PMAX, &rmax,
				    XDF_CF_UNIT, &runit,
				    XDF_CF_TRANSDUCTER, &rtransducer,
				    XDF_CF_PREFILTERING, &rprefiltering,
				      XDF_NOF);
			if (egd_channel_info(dev, s, i, EGD_LABEL, tlabel, 
			                    EGD_MM_D, &tmm, 
					    EGD_UNIT, tunit,
					    EGD_TRANSDUCER, ttransducer,
					    EGD_PREFILTERING, tprefiltering,
					    EGD_EOL))
				return -1;
			if (strcmp(tlabel, rlabel) || strcmp(tunit, runit) 
			   || strcmp(ttransducer, rtransducer) 
			   || strcmp(tprefiltering, rprefiltering) 
			   || rmin != tmm[0] || rmax != tmm[1]) {
			   	fprintf(stderr, "bad chinfo returned\n");
			  	return -1;
			}
		}
	}

	return 0;
}


static
int print_cap(struct eegdev* dev)
{
	unsigned int sampling_freq, eeg_nmax, sensor_nmax, trigger_nmax;
	char *device_type, *device_id;
	int retval;

	egd_get_cap(dev, EGD_CAP_DEVTYPE, &device_type);
	egd_get_cap(dev, EGD_CAP_DEVID, &device_id);
	egd_get_cap(dev, EGD_CAP_FS, &sampling_freq);
	eeg_nmax = egd_get_numch(dev, egd_sensor_type("eeg"));
	sensor_nmax = egd_get_numch(dev, egd_sensor_type("undefined"));
	trigger_nmax = egd_get_numch(dev, egd_sensor_type("trigger"));
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
	       "\t\tnum trigger channels: %u\n",
	       egd_get_string(),
	       device_type, device_id,
	       sampling_freq, eeg_nmax, sensor_nmax, trigger_nmax);

	return retval;
}


int test_eegsignal(char genfilename[])
{
	struct eegdev* dev;
	struct xdf* xdf;
	size_t strides[3] = {
		NEEGT*sizeof(scaled_t),
		NEXGT*sizeof(scaled_t),
		NTRIT*sizeof(int32_t)
	};
	scaled_t *eeg_r, *exg_r, *eeg_t, *exg_t;
	int32_t *tri_r, *tri_t;
	int i, j, retcode = 1;
	int ns;
	char devstring[256];

	eeg_r = calloc(NSAMPLE*NEEGT,sizeof(*eeg_r));
	eeg_t = calloc(NSAMPLE*NEEGT,sizeof(*eeg_t));
	exg_r = calloc(NSAMPLE*NEXGT,sizeof(*exg_r));
	exg_t = calloc(NSAMPLE*NEXGT,sizeof(*exg_t));
	tri_r = calloc(NSAMPLE*NTRIT,sizeof(*tri_r));
	tri_t = calloc(NSAMPLE*NTRIT,sizeof(*tri_t));

	xdf = setup_testfile(genfilename);
	sprintf(devstring, "device=datafile\npath=%s", genfilename);
	if ( !(dev = egd_open(devstring)) )
		goto exit;

	print_cap(dev);

	if (test_chinfo(dev, xdf))
		goto exit;

	if (egd_acq_setup(dev, 3, strides, 3, grp))
	    	goto exit;

	if (egd_start(dev))
		goto exit;
	
	i = 0;
	while (i < NSAMPLE*NITERATION) {
		ns = xdf_read(xdf, NSAMPLE, eeg_r, exg_r, tri_r);
		if (ns < 0)
			goto exit;
		i += ns;
		if (egd_get_data(dev, ns, eeg_t, exg_t, tri_t) < 0)
			goto exit;

		for (j=0; j<ns; j++) {
			if (memcmp(eeg_t+j*NEEGT, eeg_r+j*NEEGT, NEEGT*sizeof(*eeg_r))
			   || memcmp(exg_t+j*NEXGT, exg_r+j*NEXGT, NEXGT*sizeof(*exg_r))
			   || memcmp(tri_t+j*NTRIT, tri_r+j*NTRIT, NTRIT*sizeof(*tri_r)) ) {
			   	fprintf(stderr, "error: data differs at %i\n",
				               (i-ns)+j);
				retcode = 2;
				goto exit;
			}
		}
	}

	if (egd_stop(dev))
		goto exit;

	if (egd_close(dev))
		goto exit;
	dev = NULL;

	retcode = 0;
exit:
	if (retcode == 1)
		fprintf(stderr, "error caught: %s\n",strerror(errno));

	if (dev)
		egd_close(dev);
	xdf_close(xdf);
	free(eeg_t);
	free(eeg_r);
	free(exg_t);
	free(exg_r);
	free(tri_t);
	free(tri_r);

	return retcode;
}


int main(int argc, char *argv[])
{
	int retcode = 0, keep_file = 0, opt;
	char genfilename[] = "eegsource.bdf";

	while ((opt = getopt(argc, argv, "kv:")) != -1) {
		switch (opt) {
		case 'k':
			keep_file = 1;
			break;

		case 'v':
			verbose = atoi(optarg);
			break;

		default:	/* '?' */
			fprintf(stderr, "Usage: %s [-k keepfile] [-v verbosity]\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	// Test generation of a file
	unlink(genfilename);
	errno = 0; // reset errno to 0 in case the file did not exist
	generate_bdffile(genfilename);
	retcode = test_eegsignal(genfilename);

	if (!keep_file)
		unlink(genfilename);


	return retcode;
}


