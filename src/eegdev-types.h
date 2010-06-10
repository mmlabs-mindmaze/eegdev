#ifndef EEGDEV_TYPES_H
#define EEGDEV_TYPES_H

#include "eegdev.h"


union scale {
	float fval;
	double dval;
	uint32_t i32val;
};

typedef void (*cast_function)(void* out, const void* in, union scale sc, size_t len);

cast_function get_cast_fn(unsigned int intypes, unsigned int outtype, unsigned int scaling);

#endif	//EEGDEV_TYPES_H
