#ifndef EEGDEV_TYPES_H
#define EEGDEV_TYPES_H

#include "eegdev.h"


union scale {
	float fval;
	double dval;
	uint32_t i32val;
};

typedef void (*cast_function)(void* out, const void* in, union scale sc, size_t len);

#endif	//EEGDEV_TYPES_H
