#ifndef EEGDEV_TYPES_H
#define EEGDEV_TYPES_H

#include "eegdev.h"


union scale {
	float fval;
	double dval;
	uint32_t i32val;
};

typedef void (*cast_function)(void* restrict out, const void* restrict in, union scale sc, size_t len);

unsigned int egd_get_data_size(unsigned int types);
cast_function egd_get_cast_fn(unsigned int intypes, unsigned int outtype, unsigned int scaling);

#endif	//EEGDEV_TYPES_H
