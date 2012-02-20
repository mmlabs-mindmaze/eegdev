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
#if HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef HAVE_LOADLIBRARY

#include <windows.h>

LOCAL_FN
void *dlopen(const char *filename, int flag)
{
	(void)flag;
	return LoadLibrary(filename);
}


LOCAL_FN
void *dlsym(void *handle, const char *symbol)
{
	void* pointer;
	FARPROC WINAPI address;

	address = GetProcAddress(handle, symbol);

	memcpy(&pointer, &address, sizeof(pointer));
	return pointer;
}


LOCAL_FN
int dlclose(void *handle)
{
	return !FreeLibrary(handle);
}

#else // HAVE_LOADLIBRARY
# error No replacement found for dlopen
#endif

