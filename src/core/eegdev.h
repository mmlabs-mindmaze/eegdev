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
#ifndef EEGDEV_H
#define EEGDEV_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif 

/*************************************************************************
 *                          API definitions                              *
 ************************************************************************/
/* Supported data types */
#define EGD_INT32	0
#define EGD_FLOAT	1
#define EGD_DOUBLE	2
#define EGD_NUM_DTYPE	3

/* DEPRECATED: Do not use those constants in newly written code, use
   egd_sensor_type function */
#define EGD_EEG		0
#define EGD_TRIGGER	1
#define EGD_SENSOR	2
#define EGD_NUM_STYPE	3

/* Supported fields */
#define EGD_EOL			0
#define EGD_LABEL		1
#define EGD_MM_I		2
#define EGD_MM_F		3
#define EGD_MM_D		4
#define EGD_ISINT		5
#define EGD_UNIT		6
#define EGD_TRANSDUCTER		7
#define EGD_PREFILTERING	8
#define EGD_NUM_FIELDS		9

/* Supported capabilities fields */
#define EGD_CAP_FS		0
#define EGD_CAP_TYPELIST	1
#define EGD_CAP_DEVTYPE		2
#define EGD_CAP_DEVID		3
#define EGD_NCAP		4

struct eegdev;

struct grpconf {
	int sensortype;
	unsigned int index;
	unsigned int nch;
	unsigned int iarray;
	unsigned int arr_offset;
	int datatype;
};

int egd_sensor_type(const char* name);
const char* egd_sensor_name(int stype);

struct eegdev* egd_open(const char* conf);
int egd_get_cap(const struct eegdev* dev, int cap, void* val);
int egd_get_numch(const struct eegdev* dev, int stype);
int egd_channel_info(const struct eegdev* dev, int stype,
                     unsigned int index, int fieldtype, ...);
int egd_close(struct eegdev* dev);
int egd_acq_setup(struct eegdev* dev,
                  unsigned int narr, const size_t *strides,
                  unsigned int ngrp, const struct grpconf* grp);
int egd_start(struct eegdev* dev);
ssize_t egd_get_data(struct eegdev* dev, size_t ns, ...);
ssize_t egd_get_available(struct eegdev* dev);
int egd_stop(struct eegdev* dev);
const char* egd_get_string(void);

#ifdef __cplusplus
}
#endif

#endif /* EEGDEV_H */
