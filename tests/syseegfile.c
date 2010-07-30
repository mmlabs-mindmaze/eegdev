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
static const enum xdftype trigsttype = XDFUINT24;
static const enum xdftype trigarrtype = XDFUINT32;

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
	unsigned int strides[3] = {
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
		.sensortype = EGD_EEG,
		.index = 0,
		.iarray = 0,
		.arr_offset = 0,
		.nch = NEEGT,
		.datatype = EGD_FLOAT
	},
	{
		.sensortype = EGD_SENSOR,
		.index = NEEG,
		.iarray = 1,
		.arr_offset = 0,
		.nch = NEXGT,
		.datatype = EGD_FLOAT
	},
	{
		.sensortype = EGD_TRIGGER,
		.index = NEEG + NEXG,
		.iarray = 2,
		.arr_offset = 0,
		.nch = NTRIT,
		.datatype = EGD_INT32
	}
};

struct xdf* setup_testfile(char genfilename[])
{
	int i, offset;
	struct xdf* xdf;
	struct xdfch* ch;
	unsigned int strides[3];
	
	xdf = xdf_open(genfilename, XDF_READ, XDF_BDF);
	if (!xdf)
		return NULL;

	i = 0;
	while ((ch = xdf_get_channel(xdf, i++)))
		xdf_set_chconf(ch, XDF_CF_ARRINDEX, -1, XDF_NOF);

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

	eeg_r = calloc(NSAMPLE*NEEGT,sizeof(*eeg_r));
	eeg_t = calloc(NSAMPLE*NEEGT,sizeof(*eeg_t));
	exg_r = calloc(NSAMPLE*NEXGT,sizeof(*exg_r));
	exg_t = calloc(NSAMPLE*NEXGT,sizeof(*exg_t));
	tri_r = calloc(NSAMPLE*NTRIT,sizeof(*tri_r));
	tri_t = calloc(NSAMPLE*NTRIT,sizeof(*tri_t));

	xdf = setup_testfile(genfilename);
	if ( !(dev = egd_open_file(genfilename)) )
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

	while ((opt = getopt(argc, argv, "k")) != -1) {
		switch (opt) {
		case 'k':
			keep_file = 1;
			break;

		default:	/* '?' */
			fprintf(stderr, "Usage: %s [-k]\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	fprintf(stderr, "\tVersion : %s\n", egd_get_string());

	// Test generation of a file
	unlink(genfilename);
	generate_bdffile(genfilename);
	retcode = test_eegsignal(genfilename);

	if (!keep_file)
		unlink(genfilename);


	return retcode;
}


