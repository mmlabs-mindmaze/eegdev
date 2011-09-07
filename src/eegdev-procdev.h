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
#ifndef PROCDEV_H
#define PROCDEV_H 

#include "eegdev-common.h"

#ifdef PROCDEV_CHILD
LOCAL_FN int run_eegdev_process(eegdev_open_proc open_fn,
                                int argc, char* argv[]);
#else
LOCAL_FN struct eegdev* open_procdev(const char* optv[],
                                     const char* execfilename);
#endif

#endif //PROCDEV_H

