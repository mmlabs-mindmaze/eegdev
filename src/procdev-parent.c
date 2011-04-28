/*
    Copyright (C) 2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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

#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <pthread.h>

#include "eegdev-common.h"
#include "procdev-common.h"

struct proc_eegdev {
	struct eegdev dev;
	pid_t childpid;
	int pipein, pipeout, pipedata;
	pthread_t data_thid, return_thid;
	void* databuff;
	void* inbuf;
	size_t insize;
	int stopdata;
	pthread_mutex_t datalock;
	pthread_cond_t samlencond;
	sem_t fnsem;
	int retval;
	pthread_mutex_t retvalmtx;
	struct egd_procdev_chinfo msg_chinfo;
	char* child_devtype;
	char* child_devid;
};

#define DATABUFFSIZE (64*1024)

#define get_procdev(dev_p) \
 ((struct proc_eegdev*)(((char*)(dev_p))-offsetof(struct proc_eegdev, dev)))

// procdev methods declaration
static int proc_close_device(struct eegdev* dev);
static int proc_start_acq(struct eegdev* dev);
static int proc_stop_acq(struct eegdev* dev);
static int proc_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp);
static void proc_fill_chinfo(const struct eegdev* dev, int stype,
	                     unsigned int ich, struct egd_chinfo* info);

static const struct eegdev_operations procdev_ops = {
	.close_device = proc_close_device,
	.start_acq = proc_start_acq,
	.stop_acq = proc_stop_acq,
	.set_channel_groups = proc_set_channel_groups,
	.fill_chinfo = proc_fill_chinfo
};


/******************************************************************
 *                        Procdev internals                       *
 ******************************************************************/
static
void* data_transfer_fn(void* arg)
{
	struct proc_eegdev* procdev = arg;
	void* buff = procdev->databuff;
	ssize_t rsize;
	int stop;
	pthread_mutex_t* datamtx = &(procdev->datalock);

	// Don't start data loop before dev->in_samlen is set
	// (egd_update_ringbuffer needs it)
	pthread_mutex_lock(datamtx);
	while (!procdev->dev.in_samlen && !procdev->stopdata)
		pthread_cond_wait(&(procdev->samlencond), datamtx);
	pthread_mutex_unlock(datamtx);

	for (;;) {
		pthread_mutex_lock(datamtx);
		stop = procdev->stopdata;
		pthread_mutex_unlock(datamtx);
		if (stop)
			break;
			
		// Read incoming data from the data pipe
		rsize = read(procdev->pipedata, buff, DATABUFFSIZE);
		if (rsize <= 0) 
			break;

		// Update the eegdev structure with the new data
		if (egd_update_ringbuffer(&(procdev->dev), buff, rsize))
			break;
	}

	return NULL;
}


static
void retval_from_child(struct proc_eegdev* pdev, int retval)
{
	// Read additional data if expected
	if ((retval == 0) && pdev->insize) {
		if (fullread(pdev->pipein, pdev->inbuf, pdev->insize)) 
			retval = errno;
	}
	
	// Resume the calling thread
	pdev->retval = retval;
	sem_post(&(pdev->fnsem));
}


static
void procdev_set_samlen(struct proc_eegdev* pdev, unsigned int samlen)
{
	egd_set_input_samlen(&(pdev->dev), samlen);
	pthread_cond_signal(&(pdev->samlencond));
}


static
void procdev_update_capabilities(struct proc_eegdev* pdev)
{
	int i;
	struct egd_procdev_caps caps;

	if (fullread(pdev->pipein, &caps, sizeof(caps))
	  || !(pdev->child_devtype = malloc(caps.devtype_len))
	  || !(pdev->child_devid = malloc(caps.devid_len))
	  || fullread(pdev->pipein, pdev->child_devtype, caps.devtype_len)
	  || fullread(pdev->pipein, pdev->child_devid, caps.devid_len))
		return;

	pdev->dev.cap.sampling_freq = caps.sampling_freq;
	pdev->dev.cap.device_type = pdev->child_devtype;
	pdev->dev.cap.device_id = pdev->child_devid;
	for (i=0; i<EGD_NUM_STYPE; i++)
		pdev->dev.cap.type_nch[i] = caps.type_nch[i];
}


static
void* return_info_fn(void* arg)
{
	struct proc_eegdev* procdev = arg;
	int32_t com[2]; // {command, childretval}
	int run = 1;
	
	while (run && !fullread(procdev->pipein, com, sizeof(com))) {
		switch (com[0]) {
		case PROCDEV_REPORT_ERROR:
			egd_report_error(&(procdev->dev), com[1]);
			break;
		
		case PROCDEV_SET_SAMLEN:
			procdev_set_samlen(procdev, com[1]);
			break;

		case PROCDEV_UPDATE_CAPABILITIES:
			procdev_update_capabilities(procdev);
			break;

		case PROCDEV_SET_CHANNEL_GROUPS:
		case PROCDEV_FILL_CHINFO:
		case PROCDEV_START_ACQ:
		case PROCDEV_STOP_ACQ:
		case PROCDEV_CREATION_ENDED:
			retval_from_child(procdev, com[1]);
			break;

		case PROCDEV_CLOSE_DEVICE:
			retval_from_child(procdev, com[1]);
			run = 0;
			break;
		}
	}

	return NULL;
}


static
int exec_child_call(struct proc_eegdev* procdev, int command, 
                    size_t outlen, const void* outbuf,
		    size_t inlen, void* inbuf)
{
	int retval = 0;
	int32_t com[2] = {command, outlen};

	pthread_mutex_lock(&procdev->retvalmtx);
	procdev->inbuf = inbuf;
	procdev->insize = inlen;

	// Send the command to the child
	if (fullwrite(procdev->pipeout, &com, sizeof(com))
	   || (outlen && fullwrite(procdev->pipeout, outbuf, outlen)))
		retval = -1;
	else {
		// Wait for the child to execute the call
		sem_wait(&(procdev->fnsem));
		if (procdev->retval) {
			retval = -1;
			errno = procdev->retval;
		}
	}
	pthread_mutex_unlock(&procdev->retvalmtx);

	return retval;
}


static
void execchild(const char* execfilename, int fdout, int fdin, int fddata,
               const char* optv[]) 
{
	int32_t com[2];
	int narg = 0;
	const char* prefix = LIBEXECDIR;
	const char* prefixenv = getenv("EEGDEV_PROCDIR");
	char **argv, *path;

	prefix = prefixenv ? prefixenv : prefix;

	// Get size of the list of options
	while (optv[narg++]);
			
	path = malloc(strlen(execfilename)+strlen(prefix)+1);
	argv = malloc((narg+2)*sizeof(*argv));
	if (!path || !argv)
		goto error;

	// Set the executable filename and argument
	sprintf(path, "%s/%s", prefix, execfilename);
	memcpy(argv+1, optv, narg*sizeof(*argv));
	argv[0] = path;
	argv[narg+1] = NULL;

	dup2(fdout, PIPOUT);
	dup2(fdin, PIPIN);
	dup2(fddata, PIPDATA);
	execv(path, argv);

	// if execv returns, it means it has failed
error:
	com[0] = PROCDEV_CREATION_ENDED;
	com[1] = (errno != ENOENT) ? errno : ECHILD;
	fullwrite(PIPOUT, com, sizeof(com));
	_exit(EXIT_FAILURE);
}


static
int fork_child_proc(struct proc_eegdev* pdev, const char* execfilename,
                    const char* optv[])
{
	int i, fd[3][2] = {{-1,-1},{-1,-1},{-1,-1}};
	pid_t pid;

	for (i=0; i<3; i++) {
		if (pipe(fd[i]))
			goto error;
		fcntl(fd[i][0], F_SETFD, FD_CLOEXEC);
		fcntl(fd[i][1], F_SETFD, FD_CLOEXEC);
	}

	pid = fork();
	if (pid > 0) {
		// Parent side
		pdev->childpid = pid;
		pdev->pipein = fd[0][0];
		pdev->pipeout = fd[1][1];
		pdev->pipedata = fd[2][0];
		close(fd[0][1]);
		close(fd[1][0]);
		close(fd[2][1]);
		return 0;
	} else if (pid == 0) {
		// Child side
		execchild(execfilename, fd[0][1], fd[1][0], fd[2][1], optv);
	}
error:
	// Fork failed
	for (i=0; i<3; i++)
		if (fd[i][0] >= 0) {
			close(fd[i][0]);
			close(fd[i][1]);
		}
	return -1;
}


static
int get_child_creation_retval(struct proc_eegdev* pdev)
{
	int retval = 0;

	pthread_mutex_lock(&pdev->retvalmtx);
	sem_wait(&(pdev->fnsem));
	if (pdev->retval) {
		retval = -1;
		errno = pdev->retval;
	}
	pthread_mutex_unlock(&pdev->retvalmtx);

	return retval;
}


static
int destroy_procdev(struct proc_eegdev* pdev)
{
	pdev->stopdata = 1;
	pthread_cond_signal(&(pdev->samlencond));
	pthread_join(pdev->data_thid, NULL);
	free(pdev->databuff);

	pthread_join(pdev->return_thid, NULL);

	sem_destroy(&(pdev->fnsem));
	pthread_mutex_destroy(&(pdev->retvalmtx));
	pthread_mutex_destroy(&(pdev->datalock));
	pthread_cond_destroy(&(pdev->samlencond));
	
	waitpid(pdev->childpid, NULL, WEXITED);
	close(pdev->pipein);
	close(pdev->pipeout);
	close(pdev->pipedata);

	return 0;
}


static
int init_async(struct proc_eegdev* pdev)
{
	pdev->databuff = malloc(DATABUFFSIZE);

	pdev->stopdata = 0;
	pthread_mutex_init(&(pdev->datalock), NULL);
	pthread_cond_init(&(pdev->samlencond), NULL);
	pthread_mutex_init(&(pdev->retvalmtx), NULL);
	sem_init(&(pdev->fnsem), 0, 0);

	pthread_create(&(pdev->data_thid), NULL, data_transfer_fn, pdev);
	pthread_create(&(pdev->return_thid), NULL, return_info_fn, pdev);
	return 0;
}

/******************************************************************
 *                        Procdev methods                         *
 ******************************************************************/
LOCAL_FN
struct eegdev* open_procdev(const char* optv[], const char* execfilename)
{
	int errval;
	struct proc_eegdev* procdev = NULL;

	if ( !(procdev = calloc(1,sizeof(*procdev))))
		return NULL;

	// alloc and initialize structure
	if (fork_child_proc(procdev, execfilename, optv)
	    || init_async(procdev)
	    || egd_init_eegdev(&(procdev->dev), &procdev_ops)
	    || get_child_creation_retval(procdev))
		goto error;

	return &(procdev->dev);

error:
	errval = errno;
	if (procdev->childpid>0)
		waitpid(procdev->childpid, NULL, WEXITED);
	free(procdev);
	errno = errval;
	return NULL;
}


static
int proc_start_acq(struct eegdev* dev)
{
	return exec_child_call(get_procdev(dev), PROCDEV_START_ACQ,
	                       0, NULL, 0, NULL);
}


static
int proc_stop_acq(struct eegdev* dev)
{
	return exec_child_call(get_procdev(dev), PROCDEV_STOP_ACQ,
	                       0, NULL, 0, NULL);
}


static
int proc_close_device(struct eegdev* dev)
{
	int ret;
	struct proc_eegdev* pdev = get_procdev(dev);

	ret = exec_child_call(pdev, PROCDEV_CLOSE_DEVICE, 0, NULL, 0, NULL);
	egd_destroy_eegdev(dev);
	destroy_procdev(pdev);

	return ret;
}


static
int proc_set_channel_groups(struct eegdev* dev, unsigned int ngrp,
					const struct grpconf* grp)
{
	return exec_child_call(get_procdev(dev),PROCDEV_SET_CHANNEL_GROUPS,
	                       ngrp*sizeof(*grp), grp, 
			       dev->nsel*sizeof(*(dev->selch)), dev->selch);
}

static
void proc_fill_chinfo(const struct eegdev* dev, int stype,
	                     unsigned int ich, struct egd_chinfo* info)
{
	int32_t arg[2] = {stype, ich};
	struct proc_eegdev* pdev = get_procdev(dev);
	struct egd_procdev_chinfo* chinfo = &(pdev->msg_chinfo);
	
	// Call fill_chinfo in the child process
	exec_child_call(pdev, PROCDEV_FILL_CHINFO,
	                sizeof(arg), arg, sizeof(*chinfo), chinfo);
	
	// string field can point to the returned values because it is
	// assumed that eegdev API (at least configuration related
	// functions) is protected from concurrent use.
	info->label = chinfo->label;
	info->unit = chinfo->unit;
	info->transducter = chinfo->transducter;
	info->prefiltering = chinfo->prefiltering;
	info->isint = chinfo->isint;
	info->dtype = chinfo->dtype;
	info->min = chinfo->min;
	info->max = chinfo->max;
}
