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

#include <time.h>
#include <errno.h>
#include "portable-time.h"

#ifdef HAVE_GETTIMEOFDAY

# include <sys/time.h>

static void gettimespec(struct timespec* tp)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	tp->tv_sec = tv.tv_sec;
	tp->tv_nsec = tv.tv_usec*1000;
}

#elif defined(HAVE_GETSYSTEMTIMEASFILETIME) /* !HAVE_GETTIMEOFDAY */

# include <windows.h>

/* timespec time are expressed since Epoch i.e. since January, 1, 1970
 whereas windows FILETIME since  January 1, 1601 (UTC)*/
#define FT_EPOCH (((LONGLONG)27111902 << 32) + (LONGLONG)3577643008)

static void convert_filetime_to_timespec(const FILETIME* ft,
					struct timespec* ts, int reltime)
{
	ULARGE_INTEGER bigint;
	bigint.LowPart  = ft->dwLowDateTime;
	bigint.HighPart = ft->dwHighDateTime;

	if (reltime)
		bigint.QuadPart *= -1;
	else
		bigint.QuadPart -= FT_EPOCH;
	ts->tv_sec = bigint.QuadPart / 10000000;
	ts->tv_nsec = (bigint.QuadPart % 10000000)*100;
}

static void gettimespec(struct timespec* tp)
{
	FILETIME curr;
	GetSystemTimeAsFileTime(&curr);
	convert_filetime_to_timespec(&curr, tp, 0);
}
#elif defined(HAVE__FTIME) || defined(HAVE_FTIME)  /* !HAVE_GETSYSTEMTIMEASFILETIME */

# include <sys/timeb.h>
# ifndef HAVE_FTIME
#  define ftime _ftime
#  define timeb _timeb
# endif

static void gettimespec(struct timespec* tp)
{
	struct timeb now;

	ftime(&now);
	tp->tv_sec = now.time;
	tp->tv_nsec = now.millitm*1000000;
}

#else /* !HAVE_FTIME */

# error There is no replacement for clock_gettime

#endif


LOCAL_FN
int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
	(void)clk_id;
	if (!tp) {
		errno = EFAULT;
		return -1;
	}
	gettimespec(tp);

	return 0;
}

