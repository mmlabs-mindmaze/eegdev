#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <time.h>
#include <errno.h>
#include "clock_gettime.h"

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
# include "convfiletime.h"

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

