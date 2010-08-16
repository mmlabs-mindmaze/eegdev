#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stddef.h>
#include <errno.h>

#include <time.h>
#include <errno.h>
#include "clock_nanosleep.h"

#ifdef HAVE_NANOSLEEP

#include "clock_gettime.h"
static int abs_nanosleep(const struct timespec* req)
{
	struct timespec currts, delay;
	
	// Get current time
	clock_gettime(CLOCK_REALTIME, &currts);
	
	// Compute the delay between req and currts
	if (currts.tv_sec > req->tv_sec)
		return 0;
	delay.tv_sec = req->tv_sec - currts.tv_sec;
	delay.tv_nsec = req->tv_nsec - currts.tv_nsec;
	if (delay.tv_nsec < 0) {
		delay.tv_nsec += 1000000000;
		delay.tv_sec--;
	}
	
	// Wait for the relative time
	// rerun wait if an interruption has been caught
	if (nanosleep(&delay, NULL))
		return errno;
	return 0;
}

static int rel_nanosleep(const struct timespec* req, struct timespec* rem)
{
	if (nanosleep(req, rem))
		return errno;
	return 0;
}


#elif defined(HAVE_USLEEP)

static int abs_nanosleep(const struct timespec* req)
{
	struct timespec ts;
	int64_t delay;
	
	while (1) {
		clock_gettime(CLOCK_REALTIME, &ts);
		delay = (req.tv_sec - ts.tv_sec)*1000000
		        +(req.tv_nsec - ts.tv_nsec)/1000;
		if (delay < 0)
			return 0;

		if (delay > 1000000)
			delay = 1000000;

		if (usleep(delay) == EINTR)
			return EINTR;
	}
}


static int rel_nanosleep(const struct timespec* req, struct timespec* rem)
{
	struct timespec ats;
	int ret;

	clock_gettime(CLOCK_REALTIME, &ats);
	ats.tv_sec += req->tv_sec;
	ats.tv_nsec += req->tv_nsec;
	if (ats.tv_nsec >= 1000000000) {
		ats.tv_nsec -= 1000000000;
		ats.tv_sec++;
	}
	
	ret = abs_nanosleep(&ats);
	if ((ret != EINTR) || (rem == NULL))
		return ret;
	
	clock_gettime(CLOCK_REALTIME, rem);
	rem->tv_sec -= ats.tv_sec;
	rem->tv_nsec -= ats.tv_nsec;
	if (rem->tv_nsec < 0) {
		rem->tv_nsec += 1000000000;
		rem->tv_sec++;
	}
	
	return EINTR;		
}

#elif defined(HAVE_GETSYSTEMTIMEASFILETIME)

#include <windows.h>
#include "convfiletime.h"
static int ft_nanosleep(const struct timespec* req, int reltime)
{
	HANDLE htimer;
	FILETIME ft;

	convert_timespec_to_filetime(req, &ft, reltime);
	htimer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(htimer, (LARGE_INTEGER*)&ft, 0, NULL, NULL, FALSE);

	WaitForSingleObject(htimer, INFINITE);

	CloseHandle(htimer);
	return 0;
}

static int rel_nanosleep(const struct timespec* req, struct timespec* rem)
{
	(void)rem;
	return ft_nanosleep(req, 1);
}

static int abs_nanosleep(const struct timespec* req)
{
	return ft_nanosleep(req, 0);
}

#else

#error No replacement possible for clock_nanosleep

#endif


LOCAL_FN
int clock_nanosleep(clockid_t clock_id, int flags,
                    const struct timespec *request,
		    struct timespec *remain)
{
	if (request == NULL)
		return EFAULT;
	
	if ((request->tv_nsec < 0) || (request->tv_nsec >= 1000000000)
	  || ((clock_id != CLOCK_REALTIME)&&(clock_id != CLOCK_MONOTONIC)))
		return EINVAL;

	if (flags == 0)
		return rel_nanosleep(request, remain);
	else
		return abs_nanosleep(request);
}
