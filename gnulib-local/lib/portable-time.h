/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

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
#ifndef PORTABLE_TIME_H
#define PORTABLE_TIME_H

#include <time.h>

// Include pthread header because pthread-win32 may provide in it a non
// standard struct timespec definition. This way, we are safe: we will use
// the same declaration everywhere.
#include <pthread.h>


#ifndef HAVE_STRUCT_TIMESPEC
#define HAVE_STRUCT_TIMESPEC	1
struct timespec {
	time_t	tv_sec;		/* seconds */
	long	tv_nsec;	/* nanoseconds */
};
#endif //!HAVE_DECL_STRUCT_TIMESPEC


#ifndef HAVE_CLOCKID_T
#define HAVE_CLOCKID_T	1
typedef int clockid_t;
#  define CLOCK_REALTIME	1
#  define CLOCK_MONOTONIC	2
#endif //!HAVE_DECL_CLOCKID_T



#if !HAVE_DECL_CLOCK_GETTIME
#undef HAVE_DECL_CLOCK_GETTIME
#define HAVE_DECL_CLOCK_GETTIME	1
LOCAL_FN int clock_gettime(clockid_t clk_id, struct timespec *tp);
# endif //!HAVE_DECL_CLOCK_GETTIME


#if !HAVE_DECL_CLOCK_NANOSLEEP
#undef HAVE_DECL_CLOCK_NANOSLEEP
#define HAVE_DECL_CLOCK_NANOSLEEP	1
LOCAL_FN int clock_nanosleep(clockid_t clock_id, int flags,
                             const struct timespec *request,
		             struct timespec *remain);
#define TIMER_ABSTIME	1
# endif //!HAVE_DECL_CLOCK_NANOSLEEP


#endif // PORTABLE_TIME_H
