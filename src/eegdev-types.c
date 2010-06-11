#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <string.h>
#include "eegdev-types.h"

// Prototype of a generic type castersion function
#define DEFINE_CAST_FN(fnname, tsrc, tdst)				\
static void fnname(void* d, const void* s, union scale sc, size_t len)	\
{									\
	const tsrc* src = s;						\
	tdst* dst = d;							\
	tdst scale = *((tdst*)(&sc));					\
	while(len) {							\
		*dst = scale * ((tdst)(*src));				\
		src++;							\
		dst++;							\
		len -= sizeof(*src);					\
	}								\
}						

// Prototype of a generic type castnoscersion function
#define DEFINE_CASTNOSC_FN(fnname, tsrc, tdst)				\
static void fnname(void* d, const void* s, union scale sc, size_t len)	\
{									\
	(void)sc;							\
	const tsrc* src = s;						\
	tdst* dst = d;							\
	while(len) {							\
		*dst = ((tdst)(*src));					\
		src++;							\
		dst++;							\
		len -= sizeof(*src);					\
	}								\
}						

static void identity(void* d, const void* s, union scale sc, size_t len)
{
	(void)sc;
	memcpy(d, s, len);
}

// Declaration/definition of type castersion functions
DEFINE_CAST_FN(cast_i32_i32, int32_t, int32_t)
DEFINE_CAST_FN(cast_i32_d, int32_t, double)
DEFINE_CAST_FN(cast_d_i32, double, int32_t)
DEFINE_CAST_FN(cast_i32_f, int32_t, float)
DEFINE_CAST_FN(cast_f_i32, float, int32_t)
DEFINE_CAST_FN(cast_f_d, float, double)
DEFINE_CAST_FN(cast_d_f, double, float)
DEFINE_CAST_FN(cast_f_f, float, float)
DEFINE_CAST_FN(cast_d_d, double, double)

// Declaration/definition of type castnoscersion functions
DEFINE_CASTNOSC_FN(castnosc_i32_d, int32_t, double)
DEFINE_CASTNOSC_FN(castnosc_d_i32, double, int32_t)
DEFINE_CASTNOSC_FN(castnosc_i32_f, int32_t, float)
DEFINE_CASTNOSC_FN(castnosc_f_i32, float, int32_t)
DEFINE_CASTNOSC_FN(castnosc_f_d, float, double)
DEFINE_CASTNOSC_FN(castnosc_d_f, double, float)

static cast_function convtable[3][2][3] = {
	[EGD_INT32] = {
		[0] = {[EGD_INT32] = identity, 	[EGD_FLOAT] = castnosc_i32_f, [EGD_DOUBLE] = castnosc_i32_d},
		[1] = {[EGD_INT32] = cast_i32_i32, [EGD_FLOAT] = cast_i32_f, [EGD_DOUBLE] = cast_i32_d},
	},
	[EGD_FLOAT] = {
		[0] = {[EGD_INT32] = castnosc_f_i32, [EGD_FLOAT] = identity, [EGD_DOUBLE] = castnosc_f_d},
		[1] = {[EGD_INT32] = cast_f_i32, [EGD_FLOAT] = cast_f_f, [EGD_DOUBLE] = cast_f_d},
	},
	[EGD_DOUBLE] = {
		[0] = {[EGD_INT32] = castnosc_d_i32, [EGD_FLOAT] = castnosc_d_f, [EGD_DOUBLE] = identity},
		[1] = {[EGD_INT32] = cast_d_i32, [EGD_FLOAT] = cast_d_f, [EGD_DOUBLE] = cast_d_d},
	}
};


cast_function get_cast_fn(unsigned int itype, unsigned int otype, unsigned int scaling)
{
	if ((itype >= EGD_NUM_DTYPE) || (otype >= EGD_NUM_DTYPE))
		return NULL;

	scaling = scaling ? 1 : 0;

	return convtable[itype][scaling][otype];
}
