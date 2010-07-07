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

int add_activeelec_channel(struct xdf* xdf, const char* label, int iarr, int ind)
{
	struct xdfch* ch;
	if (!(ch = xdf_add_channel(xdf)))
		return -1;

	xdf_set_chconf(ch, 
		XDF_CHFIELD_ARRAY_TYPE, arrtype,
		XDF_CHFIELD_STORED_TYPE, sttype,
		XDF_CHFIELD_ARRAY_INDEX, iarr,
		XDF_CHFIELD_ARRAY_OFFSET, (ind*sizeof(scaled_t)),
		XDF_CHFIELD_LABEL, label,
		XDF_CHFIELD_TRANSDUCTER, "Active Electrode",
		XDF_CHFIELD_PREFILTERING, "HP: DC; LP: 417 Hz",
		XDF_CHFIELD_PHYSICAL_MIN, -262144.0,
		XDF_CHFIELD_PHYSICAL_MAX, 262143.0,
		XDF_CHFIELD_DIGITAL_MIN, -8388608.0,
		XDF_CHFIELD_DIGITAL_MAX, 8388607.0,
		XDF_CHFIELD_UNIT, "uV",
		XDF_CHFIELD_RESERVED, "EEG",
		XDF_CHFIELD_NONE);

	return 0;
}

int add_trigger_channel(struct xdf* xdf, const char* label, int iarr, int ind)
{
	struct xdfch* ch;
	if (!(ch = xdf_add_channel(xdf)))
		return -1;

	xdf_set_chconf(ch, 
		XDF_CHFIELD_ARRAY_TYPE, trigarrtype,
		XDF_CHFIELD_STORED_TYPE, trigsttype,
		XDF_CHFIELD_ARRAY_INDEX, iarr,
		XDF_CHFIELD_ARRAY_OFFSET, (ind*sizeof(int32_t)),
		XDF_CHFIELD_LABEL, label,
		XDF_CHFIELD_TRANSDUCTER, "Triggers and Status",
		XDF_CHFIELD_PREFILTERING, "No filtering",
		XDF_CHFIELD_PHYSICAL_MIN, -8388608.0,
		XDF_CHFIELD_PHYSICAL_MAX, 8388607.0,
		XDF_CHFIELD_DIGITAL_MIN, -8388608.0,
		XDF_CHFIELD_DIGITAL_MAX, 8388607.0,
		XDF_CHFIELD_UNIT, "Boolean",
		XDF_CHFIELD_RESERVED, "TRI",
		XDF_CHFIELD_NONE);

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
	xdf_set_conf(xdf, XDF_FIELD_RECORD_DURATION, (double)1.0,
			  XDF_FIELD_NSAMPLE_PER_RECORD, (int)SAMPLINGRATE,
			  XDF_FIELD_NONE);

	for (j=0; j<NEEG; j++) {
		sprintf(tmpstr, "EEG%i", j);
		if (add_activeelec_channel(xdf, tmpstr, 0, j) < 0)
			goto exit;
	}
	for (j=0; j<NEXG; j++) {
		sprintf(tmpstr, "EXG%i", j);
		if (add_activeelec_channel(xdf, tmpstr, 1, j) < 0)
			goto exit;
	}
	for (j=0; j<NTRI; j++) {
		sprintf(tmpstr, "TRI%i", j);
		if (add_trigger_channel(xdf, tmpstr, 2, j) < 0)
			goto exit;
	}

	// Make the the file ready for accepting samples
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
		.nch = NEEG,
		.datatype = EGD_FLOAT
	},
	{
		.sensortype = EGD_SENSOR,
		.index = NEEG,
		.iarray = 1,
		.arr_offset = 0,
		.nch = NEXG,
		.datatype = EGD_FLOAT
	},
	{
		.sensortype = EGD_TRIGGER,
		.index = NEEG + NEXG,
		.iarray = 2,
		.arr_offset = 0,
		.nch = NTRI,
		.datatype = EGD_INT32
	}
};

struct xdf* setup_testfile(char genfilename[])
{
	int i, offset;
	struct xdf* xdf;
	unsigned int strides[3];
	
	xdf = xdf_open(genfilename, XDF_READ, XDF_BDF);

	offset = 0;
	for (i=0; i<NEEG; i++) {
		xdf_set_chconf(xdf_get_channel(xdf, i),
			XDF_CHFIELD_ARRAY_DIGITAL, 0,
			XDF_CHFIELD_ARRAY_INDEX, 0,
			XDF_CHFIELD_ARRAY_TYPE, XDFFLOAT,
			XDF_CHFIELD_ARRAY_OFFSET, offset,
			XDF_CHFIELD_NONE);
		offset += sizeof(float);
	}
	strides[0] = offset;

	offset = 0;
	for (i=0; i<NEXG; i++) {
		xdf_set_chconf(xdf_get_channel(xdf, i+NEEG),
			XDF_CHFIELD_ARRAY_DIGITAL, 0,
			XDF_CHFIELD_ARRAY_INDEX, 1,
			XDF_CHFIELD_ARRAY_TYPE, XDFFLOAT,
			XDF_CHFIELD_ARRAY_OFFSET, offset,
			XDF_CHFIELD_NONE);
		offset += sizeof(float);
	}
	strides[1] = offset;

	offset = 0;
	for (i=0; i<NTRI; i++) {
		xdf_set_chconf(xdf_get_channel(xdf, i+NEEG+NEXG),
			XDF_CHFIELD_ARRAY_DIGITAL, 0,
			XDF_CHFIELD_ARRAY_INDEX, 2,
			XDF_CHFIELD_ARRAY_TYPE, XDFINT32,
			XDF_CHFIELD_ARRAY_OFFSET, offset,
			XDF_CHFIELD_NONE);
		offset += sizeof(int32_t);
	}
	strides[2] = offset;

	xdf_define_arrays(xdf, 3, strides);
	xdf_prepare_transfer(xdf);

	return xdf;
}

int test_eegsignal(char genfilename[])
{
	struct eegdev* dev;
	struct xdf* xdf;
	size_t strides[3] = {
		NEEG*sizeof(scaled_t),
		NEXG*sizeof(scaled_t),
		NTRI*sizeof(int32_t)
	};
	scaled_t *eeg_r, *exg_r, *eeg_t, *exg_t;
	int32_t *tri_r, *tri_t;
	int i, j, retcode = 1;
	int ns;

	eeg_r = calloc(NSAMPLE*NEEG,sizeof(*eeg_r));
	eeg_t = calloc(NSAMPLE*NEEG,sizeof(*eeg_t));
	exg_r = calloc(NSAMPLE*NEXG,sizeof(*exg_r));
	exg_t = calloc(NSAMPLE*NEXG,sizeof(*exg_t));
	tri_r = calloc(NSAMPLE*NTRI,sizeof(*tri_r));
	tri_t = calloc(NSAMPLE*NTRI,sizeof(*tri_t));

	xdf = setup_testfile(genfilename);
	if ( !(dev = egd_open_file(genfilename)) )
		goto exit;

	if (egd_set_groups(dev, 3, grp)
	    || egd_decl_arrays(dev, 3, strides) )
	    	goto exit;

	if (egd_start(dev))
		goto exit;
	
	i = 0;
	while (i < NSAMPLE*NITERATION) {
		ns = xdf_read(xdf, NSAMPLE, eeg_r, exg_r, tri_r);
		if (ns < 0)
			goto exit;
		i += ns;
		if (egd_get_data(dev, ns, eeg_t, exg_t, tri_t))
			goto exit;

		for (j=0; j<ns; j++) {
		if (memcmp(eeg_t+j*NEEG, eeg_r+j*NEEG, NEEG*sizeof(*eeg_r))
		   || memcmp(exg_t+j*NEXG, exg_r+j*NEXG, NEXG*sizeof(*exg_r))
		   || memcmp(tri_t+j*NTRI, tri_r+j*NTRI, NTRI*sizeof(*tri_r)) ) {
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


