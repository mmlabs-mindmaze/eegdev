/*
	Copyright (C) 2010  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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
