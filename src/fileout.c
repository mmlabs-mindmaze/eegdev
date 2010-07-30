#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <xdfio.h>
#include <errno.h>
#include <time.h>


#include "eegdev-types.h"
#include "eegdev-common.h"

struct xdfout_eegdev {
	struct eegdev dev;
	pthread_t thread_id;
	pthread_cond_t runcond;
	pthread_mutex_t runmtx;
	sem_t reading;
	int runstate;

	void* chunkbuff;
	size_t chunksize;
	struct xdf* xdf;
	struct timespec start_ts;
};
#define CHUNK_NS	4

#define READ_STOP	0
#define READ_RUN	1
#define READ_EXIT	2

#define get_xdf(dev_p) \
	((struct xdfout_eegdev*)(((char*)(dev_p))-offsetof(struct xdfout_eegdev, dev)))

// xdffileout methods declaration
static int xdfout_close_device(struct eegdev* dev);
static int xdfout_start_acq(struct eegdev* dev);
static int xdfout_stop_acq(struct eegdev* dev);
static int xdfout_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);

static const struct eegdev_operations biosemi_ops = {
	.close_device = xdfout_close_device,
	.start_acq = xdfout_start_acq,
	.stop_acq = xdfout_stop_acq,
	.set_channel_groups = xdfout_set_channel_groups,
};

unsigned int dattab[EGD_NUM_DTYPE] = {
	[EGD_INT32] = XDFINT32,
	[EGD_FLOAT] = XDFFLOAT,
	[EGD_DOUBLE] = XDFDOUBLE,
};


/******************************************************************
 *                  Internals implementation                      *
 ******************************************************************/
static void add_dtime_ns(struct timespec* ts, long delta_ns)
{
	ts->tv_nsec += delta_ns;
	if (ts->tv_nsec >= 1000000000) {
		ts->tv_nsec -= 1000000000;
		ts->tv_sec++;
	} else if (ts->tv_nsec < 0) {
		ts->tv_nsec += 1000000000;
		ts->tv_sec--;
	}
}


static void extract_file_info(struct xdfout_eegdev* xdfdev)
{
	struct xdf* xdf = xdfdev->xdf;
	int nch, fs;

	xdf_get_conf(xdf, XDF_F_SAMPLING_FREQ, &fs,
			  XDF_F_NCHANNEL, &nch,
			  XDF_NOF);

	xdfdev->dev.cap.sampling_freq = fs;

	// TODO: be smarter and interpret the label or anything else to
	// separate all channel type
	xdfdev->dev.cap.eeg_nmax = nch;
	xdfdev->dev.cap.sensor_nmax = nch;
	xdfdev->dev.cap.trigger_nmax = nch;

}


static void* file_read_fn(void* arg)
{
	struct xdfout_eegdev* xdfdev = arg;
	struct xdf* xdf = xdfdev->xdf;
	struct timespec next;
	void* chunkbuff = xdfdev->chunkbuff;
	long delay = CHUNK_NS*(1000000000 / xdfdev->dev.cap.sampling_freq);
	pthread_mutex_t* runmtx = &(xdfdev->runmtx);
	pthread_cond_t* runcond = &(xdfdev->runcond);
	ssize_t ns;
	int runstate;

	sem_wait(&(xdfdev->reading));
	clock_gettime(CLOCK_MONOTONIC, &next);
	while (1) {
		// Wait for the runstate to be different from READ_STOP
		pthread_mutex_lock(runmtx);
		while ((runstate = xdfdev->runstate) == READ_STOP) {
			pthread_cond_wait(runcond, runmtx);
			memcpy(&next, &(xdfdev->start_ts), sizeof(next));
		}
		pthread_mutex_unlock(runmtx);
		if (runstate == READ_EXIT)
			break;

		// Schedule the next data chunk availability
		add_dtime_ns(&next, delay);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

		// Read the data chunk and update the eegdev accordingly
		ns = xdf_read(xdf, CHUNK_NS, chunkbuff);
		if (ns > 0)
			egd_update_ringbuffer(&(xdfdev->dev), chunkbuff, 
		                          ns * xdfdev->dev.in_samlen);
		else {
			pthread_mutex_lock(runmtx);
			if (xdfdev->runstate == READ_RUN)
				xdfdev->runstate = READ_STOP;
			pthread_mutex_unlock(runmtx);
		}
	}
	sem_post(&(xdfdev->reading));

	return NULL;
}


static int start_reading_thread(struct xdfout_eegdev* xdfdev)
{
	int ret;

	xdfdev->runstate = READ_STOP;
	
	sem_init(&(xdfdev->reading), 0, 1);

	if ( (ret = pthread_mutex_init(&(xdfdev->runmtx), NULL))
	    || (ret = pthread_cond_init(&(xdfdev->runcond), NULL))
	    || (ret = pthread_create(&(xdfdev->thread_id), NULL, 
	                             file_read_fn, xdfdev)) ) {
		errno = ret;
		return -1;
	}

	return 0;
}


static int stop_reading_thread(struct xdfout_eegdev* xdfdev)
{

	// Order the thread to stop
	pthread_mutex_lock(&(xdfdev->runmtx));
	xdfdev->runstate = READ_EXIT;
	pthread_cond_signal(&(xdfdev->runcond));
	pthread_mutex_unlock(&(xdfdev->runmtx));

	// Wait the thread to stop and free synchronization resources
	sem_wait(&(xdfdev->reading));
	pthread_join(xdfdev->thread_id, NULL);
	pthread_cond_destroy(&(xdfdev->runcond));
	pthread_mutex_destroy(&(xdfdev->runmtx));
	sem_destroy(&(xdfdev->reading));
	return 0;
}


/******************************************************************
 *               XDF file out methods implementation              *
 ******************************************************************/
struct eegdev* egd_open_file(const char* filename)
{
	struct xdfout_eegdev* xdfdev = NULL;
	struct xdf* xdf = NULL;
	void* chunkbuff = NULL;
	int nch;
	size_t chunksize;

	if (!(xdf = xdf_open(filename, XDF_READ, XDF_ANY)))
		goto error;

	xdf_get_conf(xdf, XDF_F_NCHANNEL, &nch, XDF_NOF);
	chunksize = nch*sizeof(double)* CHUNK_NS;

	if (!(xdfdev = malloc(sizeof(*xdfdev)))
	    || !(chunkbuff = malloc(chunksize)))
		goto error;

	// Initialize structures
	egd_init_eegdev(&(xdfdev->dev), &biosemi_ops);
	xdfdev->xdf = xdf;
	xdfdev->chunkbuff = chunkbuff;
	extract_file_info(xdfdev);

	// Start reading thread
	if (start_reading_thread(xdfdev))
		goto error;

	return &(xdfdev->dev);

error:
	xdf_close(xdf);
	free(chunkbuff);
	free(xdfdev);
	return NULL;
}


static int xdfout_close_device(struct eegdev* dev)
{
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	
	stop_reading_thread(xdfdev);
	egd_destroy_eegdev(dev);

	xdf_close(xdfdev->xdf);
	free(xdfdev->chunkbuff);
	free(xdfdev);

	return 0;
}


static int xdfout_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	unsigned int i, j, type, dsize, offset = 0;
	struct selected_channels* selch = dev->selch;
	struct xdfch* ch;

	j = 0;
	while ((ch = xdf_get_channel(xdfdev->xdf, j))) {
		xdf_set_chconf(ch, XDF_CF_ARRINDEX, -1, XDF_NOF);
		j++;
	}

	for (i=0; i<ngrp; i++) {
		type = grp[i].datatype;
		dsize = egd_get_data_size(type);

		// Set parameters of (eeg -> ringbuffer)
		selch[i].in_offset = offset;
		selch[i].len = grp[i].nch * dsize;
		selch[i].cast_fn = egd_get_cast_fn(type, type, 0);

		// Set XDF channel configuration
		for (j=grp[i].index; j<grp[i].nch+grp[i].index; j++) {
			ch = xdf_get_channel(xdfdev->xdf, j);
			xdf_set_chconf(ch, 
			               XDF_CF_ARRTYPE, dattab[type],
			               XDF_CF_ARRINDEX, 0,
				       XDF_CF_ARROFFSET, offset,
				       XDF_CF_ARRDIGITAL, 0,
				       XDF_NOF);
			offset += dsize;
		}
	}
	xdfdev->dev.in_samlen = offset;
	xdf_define_arrays(xdfdev->xdf, 1, &offset);
	xdf_prepare_transfer(xdfdev->xdf);
		
	return 0;
}


static int xdfout_start_acq(struct eegdev* dev)
{
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	pthread_mutex_lock(&(xdfdev->runmtx));

	xdf_seek(xdfdev->xdf, 0, SEEK_SET);
	memcpy(&(xdfdev->start_ts), &ts, sizeof(ts));

	xdfdev->runstate = READ_RUN;
	pthread_cond_signal(&(xdfdev->runcond));

	pthread_mutex_unlock(&(xdfdev->runmtx));

	return 0;
}


static int xdfout_stop_acq(struct eegdev* dev)
{
	struct xdfout_eegdev* xdfdev = get_xdf(dev);
	
	pthread_mutex_lock(&(xdfdev->runmtx));
	
	xdfdev->runstate = READ_STOP;
	pthread_cond_signal(&(xdfdev->runcond));
	
	pthread_mutex_unlock(&(xdfdev->runmtx));

	return 0;
}
