#if HAVE_CONFIG_H
#include <config.h>
#endif

// This function exists to make sure that libportability.la is not empty:
// empty convenience library is not portable
LOCAL_FN void portability_dummy_fn(void) {}

