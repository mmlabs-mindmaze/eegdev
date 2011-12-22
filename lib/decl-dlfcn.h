/*
    Copyright (C) 2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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
#ifndef DECL_DLFCN_H
#define DECL_DLFCN_H

#if HAVE_DLFCN_H

#include <dlfcn.h>

#else //HAVE_DLFCN_H

#define RTLD_LAZY	0
#define RTLD_LOCAL	0

LOCAL_FN void *dlopen(const char *filename, int flag);
LOCAL_FN void *dlsym(void *handle, const char *symbol);
LOCAL_FN int dlclose(void *handle);

#endif //HAVE_DLFCN_H

#endif //DECL_DLFCN_H

