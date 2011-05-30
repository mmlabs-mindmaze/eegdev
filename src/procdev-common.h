/*
    Copyright (C) 2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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
#ifndef PROCDEV_COMMON_H
#define PROCDEV_COMMON_H 

#include "eegdev-common.h"
#include "eegdev-procdev.h"

#define PROCDEV_REPORT_ERROR		0
#define PROCDEV_SET_SAMLEN		1
#define PROCDEV_UPDATE_CAPABILITIES	2
#define PROCDEV_CLOSE_DEVICE		3
#define PROCDEV_SET_CHANNEL_GROUPS	4
#define PROCDEV_START_ACQ		5
#define PROCDEV_STOP_ACQ		6
#define PROCDEV_FILL_CHINFO		7
#define PROCDEV_CREATION_ENDED		8

#pragma pack(push, 1)
struct egd_procdev_chinfo {
	char label[EGD_LABEL_LEN];
	char unit[EGD_UNIT_LEN];
	char transducter[EGD_TRANSDUCTER_LEN];
	char prefiltering[EGD_PREFILTERING_LEN];
	int32_t isint;
	int32_t dtype;
	union gval min, max;
};

struct egd_procdev_caps {
	uint32_t sampling_freq;
	uint32_t type_nch[EGD_NUM_STYPE];
	uint32_t devtype_len, devid_len;
};
#pragma pack(pop)

// Definition of the file descriptor used by the child
#define PIPIN	STDIN_FILENO
#define PIPOUT	STDOUT_FILENO
#define PIPDATA	4

// Procdev utilities
static
int fullread(int fd, void* buff, size_t count)
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

static
int fullwrite(int fd, const void* buff, size_t count)
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


#endif //PROCDEV_COMMON_H
