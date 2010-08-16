#ifndef CLOCK_GETTIME_H
#define CLOCK_GETTIME_H

#include <time.h>

#if !HAVE_DECL_CLOCK_GETTIME
# include "timespec.h"

LOCAL_FN
int clock_gettime(clockid_t clk_id, struct timespec *tp);
# endif //!HAVE_DECL_CLOCK_GETTIME


#endif /* CLOCK_GETTIME_H */

