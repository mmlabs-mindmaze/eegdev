/*
	Copyright (C) 2010  EPFL (Ecole Polytechnique Fédérale de Lausanne)
	Nicolas Bourdaud <nicolas.bourdaud@epfl.ch>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
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
