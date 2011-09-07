/*
    Copyright (C) 2011  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
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
#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <eegdev.h>
#include "../src/device-helper.h"
#include "../src/eegdev-common.h"
#include "../src/eegdev-types.h"

static
struct egdich channels[] = {
	{.dtype = EGD_FLOAT, .stype = 0},
	{.dtype = EGD_FLOAT, .stype = 0},
	{.dtype = EGD_FLOAT, .stype = 0},
	{.dtype = EGD_INT32, .stype = 1},
	{.dtype = EGD_INT32, .stype = 1},
	{.dtype = EGD_FLOAT, .stype = 1},
	{.dtype = EGD_DOUBLE, .stype = 0},
	{.dtype = EGD_DOUBLE, .stype = 0},
	{.dtype = EGD_FLOAT, .stype = 0},
	{.dtype = EGD_FLOAT, .stype = 2},
	{.dtype = EGD_FLOAT, .stype = 2},
	{.dtype = EGD_FLOAT, .stype = 0},
	{.dtype = EGD_FLOAT, .stype = 0},
	{.dtype = EGD_FLOAT, .stype = 0},
};
#define NCH	(sizeof(channels)/sizeof(channels[0]))

static
struct grpconf grp[] = {
	{.sensortype = 2, .index = 1, .nch = 1, .iarray = 1, .arr_offset = 0, .datatype = EGD_DOUBLE},
	{.sensortype = 1, .index = 0, .nch = 3, .iarray = 0, .arr_offset = 0, .datatype = EGD_FLOAT},
	{.sensortype = 0, .index = 2, .nch = 7, .iarray = 0, .arr_offset = 3*4, .datatype = EGD_FLOAT},
};
#define NGRP	(sizeof(grp)/sizeof(grp[0]))

struct selected_channels expected_selch[] = {
	{.in_offset = (6+2)*4 + 2*8, .inlen = 4, .typein = EGD_FLOAT, .typeout = EGD_DOUBLE, .arr_offset = 0, .iarray = 1},
	{.in_offset = 3*4, .inlen = 2*4, .typein = EGD_INT32, .typeout = EGD_FLOAT, .arr_offset = 0, .iarray = 0},
	{.in_offset = (3+2)*4, .inlen = 4, .typein = EGD_FLOAT, .typeout = EGD_FLOAT, .arr_offset = 2*4, .iarray = 0},
	{.in_offset = 2*4, .inlen = 4, .typein = EGD_FLOAT, .typeout = EGD_FLOAT, .arr_offset = 3*4, .iarray = 0},
	{.in_offset = (4+2)*4, .inlen = 2*8, .typein = EGD_DOUBLE, .typeout = EGD_FLOAT, .arr_offset = 4*4, .iarray = 0},
	{.in_offset = (4+2)*4 + 2*8, .inlen = 4, .typein = EGD_FLOAT, .typeout = EGD_FLOAT, .arr_offset = 6*4, .iarray = 0},
	{.in_offset = (7+2)*4 + 2*8, .inlen = 3*4, .typein = EGD_FLOAT, .typeout = EGD_FLOAT, .arr_offset = 7*4, .iarray = 0},
};
#define NEXPSELCH	(sizeof(expected_selch)/sizeof(expected_selch[0]))

static
int test_split(struct eegdev* dev)
{
	int nsel;

	nsel = egdi_split_alloc_chgroups(dev, channels, NGRP, grp);
	if (nsel != NEXPSELCH)
		return -1;
	return memcmp(expected_selch, dev->selch, sizeof(expected_selch));
}


static
int dummy_close_device(struct eegdev* dev)
{
	(void) dev;
	return 0;
}


int main(void)
{
	int retval;
	struct eegdev dev = {.error = 0};
	struct eegdev_operations ops = {.close_device = dummy_close_device};

	egd_init_eegdev(&dev, &ops);
	retval = test_split(&dev);
	egd_destroy_eegdev(&dev);

	return (retval == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

