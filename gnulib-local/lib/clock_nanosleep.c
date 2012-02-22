/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stddef.h>
#include <errno.h>

#include <time.h>
#include <errno.h>
#include "portable-time.h"

#ifdef HAVE_NANOSLEEP

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

/* timespec time are expressed since Epoch i.e. since January, 1, 1970
 whereas windows FILETIME since  January 1, 1601 (UTC)*/
#define FT_EPOCH (((LONGLONG)27111902 << 32) + (LONGLONG)3577643008)

static void convert_timespec_to_filetime(const struct timespec* ts,
					FILETIME* ft, int reltime)
{
	ULARGE_INTEGER bigint;
	bigint.QuadPart = (LONGLONG)ts->tv_sec*10000000
			+ ((LONGLONG)ts->tv_nsec + 50)/100;
	if (reltime)
		bigint.QuadPart *= -1;
	else
		bigint.QuadPart += FT_EPOCH;
	ft->dwLowDateTime = bigint.LowPart;
	ft->dwHighDateTime = bigint.HighPart;
}


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
