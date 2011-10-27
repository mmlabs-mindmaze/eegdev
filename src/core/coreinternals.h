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
#ifndef COREINTERNALS_H
#define COREINTERNALS_H

#include <stdint.h>
#include "eegdev.h"
#include "eegdev-common.h"

LOCAL_FN void egd_update_capabilities(struct eegdev* dev);
LOCAL_FN void egd_destroy_eegdev(struct eegdev* dev);
LOCAL_FN struct eegdev* egdi_create_eegdev(const struct egdi_plugin_info* info);

#define get_typed_val(gval, type) 			\
((type == EGD_INT32) ? gval.valint32_t : 			\
	(type == EGD_FLOAT ? gval.valfloat : gval.valdouble))

LOCAL_FN
cast_function egd_get_cast_fn(unsigned int intypes, unsigned int outtype,
                              unsigned int scaling);


#endif	//COREINTERNALS_H
