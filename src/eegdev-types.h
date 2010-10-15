#ifndef EEGDEV_TYPES_H
#define EEGDEV_TYPES_H

#include <stdint.h>
#include "eegdev.h"


union gval {
	float fval;
	double dval;
	uint32_t i32val;
};

#define get_typed_val(gval, type) 			\
((type == EGD_INT32) ? gval.i32val : 			\
	(type == EGD_FLOAT ? gval.fval : gval.dval))

typedef void (*cast_function)(void* restrict out, const void* restrict in, union gval sc, size_t len);

unsigned int egd_get_data_size(unsigned int types);
cast_function egd_get_cast_fn(unsigned int intypes, unsigned int outtype, unsigned int scaling);

#endif	//EEGDEV_TYPES_H
