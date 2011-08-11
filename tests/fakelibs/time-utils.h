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
#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <time.h>

static inline
int addtime(struct timespec* ts, long sec, long nsec)
{
	if ((nsec >= 1000000000) || (nsec <= -1000000000)) {
		errno = EINVAL;
		return -1;
	}

	ts->tv_sec += sec;
	ts->tv_nsec += nsec;
	if (ts->tv_nsec >= 1000000000) {
		ts->tv_nsec -= 1000000000;
		ts->tv_sec += 1;
	} else if (ts->tv_nsec < 0) {
		ts->tv_nsec += 1000000000;
		ts->tv_sec -= 1;
	}

	return 0;
}

static inline
long difftime_ms(const struct timespec* ts, const struct timespec* orig)
{
	long diff = (ts->tv_sec - orig->tv_sec)*1000;
	diff += (ts->tv_nsec - orig->tv_nsec)/1000000;
	return diff;
}

#endif

