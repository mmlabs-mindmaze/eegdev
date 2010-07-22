//#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <eegdev.h>

#define SAMPLINGRATE	128	// in Hz
#define DURATION	4	// in seconds
#define NITERATION	((SAMPLINGRATE*DURATION)/NSAMPLE)
#define NSAMPLE	17
#define NEEG	64
#define NEXG	24
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
		.iarray = 1,
		.arr_offset = 0,
		.nch = NEXG,
		.datatype = EGD_FLOAT
	},
	{
		.sensortype = EGD_TRIGGER,
		.index = 0,
		.iarray = 2,
		.arr_offset = 0,
		.nch = NTRI,
		.datatype = EGD_INT32
	}
};


int read_eegsignal(void)
{
	struct eegdev* dev;
	size_t strides[3] = {
		NEEG*sizeof(scaled_t),
		NEXG*sizeof(scaled_t),
		NTRI*sizeof(int32_t)
	};
	scaled_t *eeg_t, *exg_t;
	int32_t *tri_t, triref;
	int i, j, retcode = 1;

	eeg_t = calloc(NSAMPLE*NEEG,sizeof(*eeg_t));
	exg_t = calloc(NSAMPLE*NEXG,sizeof(*exg_t));
	tri_t = calloc(NSAMPLE*NTRI,sizeof(*tri_t));

	if ( !(dev = egd_open_biosemi()) )
		goto exit;

	if (egd_set_groups(dev, 3, grp)
	    || egd_decl_arrays(dev, 3, strides) )
	    	goto exit;

	if (egd_start(dev))
		goto exit;
	
	i = 0;
	while (i < NSAMPLE*NITERATION) {
		if (egd_get_data(dev, NSAMPLE, eeg_t, exg_t, tri_t))
			goto exit;

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
		fprintf(stderr, "error caught (%i) %s\n",errno,strerror(errno));

	egd_close(dev);
	free(eeg_t);
	free(exg_t);
	free(tri_t);

	return retcode;
}


int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	int retcode = 0;

	fprintf(stderr, "\tTesting biosemi\n\tVersion : %s\n", egd_get_string());

	// Test generation of a file
	retcode = read_eegsignal();


	return retcode;
}



