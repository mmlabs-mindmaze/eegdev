/*
    Copyright (C) 2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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

#include <errno.h>
#include <unistd.h>
#include "device-helper.h"


LOCAL_FN
int egdi_fullread(int fd, void* buff, size_t count)
{
	do {
		ssize_t rsiz = read(fd, buff, count);
		if (rsiz <= 0) {
			if (rsiz == 0)
				errno = EPIPE;
			return -1;
		}
		count -= rsiz;
		buff = ((char*)buff) + rsiz;
	} while(count);
	return 0;
}


LOCAL_FN
int egdi_fullwrite(int fd, const void* buff, size_t count)
{
	do {
		ssize_t rsiz = write(fd, buff, count);
		if (rsiz < 0)
			return -1;
		count -= rsiz;
		buff = ((char*)buff) + rsiz;
	} while(count);
	return 0;
}


