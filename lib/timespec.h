/*
	Copyright (C) 2009  EPFL (Ecole Polytechnique Fédérale de Lausanne)
	Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This file is part of the act2demux library

    The act2demux library is free software: you can redistribute it and/or
    modify it under the terms of the version 3 of the GNU General Public
    License as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef _TIMESPEC_H_
#define _TIMESPEC_H_

#include <time.h>

#ifndef HAVE_STRUCT_TIMESPEC
struct timespec {
	time_t	tv_sec;		/* seconds */
	long	tv_nsec;	/* nanoseconds */
};
#endif //!HAVE_DECL_STRUCT_TIMESPEC

# ifndef HAVE_CLOCKID_T
typedef int clockid_t;
#  define CLOCK_REALTIME	1
#  define CLOCK_MONOTONIC	2
# endif //!HAVE_DECL_CLOCKID_T

#endif //_TIMESPEC_H_
