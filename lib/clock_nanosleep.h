#ifndef CLOCK_NANOSLEEP_H
#define CLOCK_NANOSLEEP_H

#include <time.h>

#if !HAVE_DECL_CLOCK_NANOSLEEP
# include "timespec.h"

#define TIMER_ABSTIME 1

LOCAL_FN
int clock_nanosleep(clockid_t clock_id, int flags,
                    const struct timespec *request,
		    struct timespec *remain);
# endif //!HAVE_DECL_CLOCK_NANOSLEEP

#endif /* CLOCK_NANOSLEEP_H */
