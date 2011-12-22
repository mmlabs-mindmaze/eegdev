/*
    Copyright (C) 2010-2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

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
#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <string.h>
#include "coreinternals.h"

// Prototype of a generic type scale and cast function
#define DEFINE_CAST_FN(tsrc, tdst)			\
static void cast_##tsrc##_##tdst (void* restrict d, const void* restrict s, union gval sc, size_t len)	\
{									\
	const tsrc* src = s;						\
	tdst* dst = d;							\
	tdst scale = sc.val##tdst ;					\
	while(len) {							\
		*dst = scale * ((tdst)(*src));				\
		src++;							\
		dst++;							\
		len -= sizeof(*src);					\
	}								\
}						

// Prototype of a generic type cast function
#define DEFINE_CASTNOSC_FN(tsrc, tdst)				\
static void castnosc_##tsrc##_##tdst (void* restrict d, const void* restrict s, union gval sc, size_t len)	\
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

static void identity(void* restrict d, const void* restrict s, union gval sc, size_t len)
{
	(void)sc;
	memcpy(d, s, len);
}

// Declaration/definition of type cast and scale functions
DEFINE_CAST_FN(int32_t, int32_t)
DEFINE_CAST_FN(int32_t, double)
DEFINE_CAST_FN(double, int32_t)
DEFINE_CAST_FN(int32_t, float)
DEFINE_CAST_FN(float, int32_t)
DEFINE_CAST_FN(float, double)
DEFINE_CAST_FN(double, float)
DEFINE_CAST_FN(float, float)
DEFINE_CAST_FN(double, double)

// Declaration/definition of type cast functions
#define castnosc_int32_t_int32_t identity
DEFINE_CASTNOSC_FN(int32_t, float)
DEFINE_CASTNOSC_FN(int32_t, double)
DEFINE_CASTNOSC_FN(float, int32_t)
#define castnosc_float_float identity
DEFINE_CASTNOSC_FN(float, double)
DEFINE_CASTNOSC_FN(double, int32_t)
DEFINE_CASTNOSC_FN(double, float)
#define castnosc_double_double identity

#define TABLE_ENTRY(egdtype, datatype) \
	[egdtype] = {							\
		[0] = {[EGD_INT32] = castnosc_##datatype##_int32_t,	\
		       [EGD_FLOAT] = castnosc_##datatype##_float,	\
		       [EGD_DOUBLE] = castnosc_##datatype##_double}, \
		[1] = {[EGD_INT32] = cast_##datatype##_int32_t, 	\
		       [EGD_FLOAT] = cast_##datatype##_float,	\
		       [EGD_DOUBLE] = cast_##datatype##_double},	\
	}
static cast_function convtable[3][2][3] = {
	TABLE_ENTRY(EGD_INT32, int32_t),
	TABLE_ENTRY(EGD_FLOAT, float),
	TABLE_ENTRY(EGD_DOUBLE, double)
};


LOCAL_FN
cast_function egd_get_cast_fn(unsigned int itype, unsigned int otype, unsigned int scaling)
{
	if ((itype >= EGD_NUM_DTYPE) || (otype >= EGD_NUM_DTYPE))
		return NULL;

	scaling = scaling ? 1 : 0;

	return convtable[itype][scaling][otype];
}
