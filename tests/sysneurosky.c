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

struct systemcap cap;

static void print_cap(void) {
	printf("\tsystem capabilities:\n"
	       "\t\tsampling frequency: %u Hz\n"
	       "\t\tnum EEG channels: %u\n"
	       "\t\tnum sensor channels: %u\n"
	       "\t\tnum trigger channels: %u\n",
	       cap.sampling_freq, cap.eeg_nmax, 
	       cap.sensor_nmax, cap.trigger_nmax);
}


int read_eegsignal(void)
{
	struct eegdev* dev;
	size_t strides[1] = {NEEG*sizeof(scaled_t)};
	scaled_t *eeg_t;
	int i, j, retcode = 1;

	eeg_t = calloc(NSAMPLE*NEEG,sizeof(*eeg_t));

	if ( !(dev = egd_open_neurosky(devpath)) )
		goto exit;

	egd_get_cap(dev, &cap);
	print_cap();
	

	if (egd_acq_setup(dev, 1, strides, 1, grp))
	    	goto exit;

	if (egd_start(dev))
		goto exit;
	
	i = 0;
	while (i < (int)cap.sampling_freq*DURATION) {
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



