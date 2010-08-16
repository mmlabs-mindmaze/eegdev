#ifndef WIN32_TIME_H
#define WIN32_TIME_H

#include <windows.h>
#include "timespec.h"

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

#endif /* WIN32_TIME_H */
