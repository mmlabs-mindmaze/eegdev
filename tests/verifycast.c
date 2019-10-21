/*
    Copyright (C) 2010-2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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
#include <mmargparse.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "src/core/eegdev-pluginapi.h"
#include "src/core/coreinternals.h"

typedef	float	scaled_t;
unsigned int orignumch = 64;
unsigned int innumch = 72;
unsigned int chunklen = 72;
unsigned int inbuff_offset = 0;
#define NS	8192
#define NPOINT	(orignumch*NS)
#define INNPOINT	(innumch*NS)
#define NGRP	3


struct mmarg_opt arg_options[] = {
	{"s", MMOPT_OPTUINT, NULL, {.uiptr = &orignumch},
		"set orig number of channels."},
	{"S", MMOPT_OPTUINT, NULL, {.uiptr = &innumch},
		"set input number of channels."},
	{"o", MMOPT_OPTUINT, NULL, {.uiptr = &inbuff_offset},
		"set input buffer offset."},
	{"c", MMOPT_OPTUINT, NULL, {.uiptr = &chunklen},
		"set chunk length."}
};


struct egdi_plugin_info info = {.struct_size = sizeof(struct eegdev)};


LOCAL_FN
const struct egdi_chinfo* egdi_get_conf_mapping(struct devmodule* mdev,
                                                const char* name, int* pnch)
{
	(void)mdev;
	(void)name;
	(void)pnch;

	return NULL;
}


void init_inbufgrp(struct input_buffer_group* ibgrp, int ngrp)
{
	int i, in_off = 0;
	unsigned int lench, remch = orignumch;

	for (i=0; i<ngrp; i++) {
		lench = remch / (ngrp-i);
		remch -= lench;
		ibgrp[i].inlen = lench*sizeof(scaled_t);
		ibgrp[i].in_offset = in_off+inbuff_offset*sizeof(scaled_t);
		ibgrp[i].buff_offset=in_off;
		ibgrp[i].cast_fn = egd_get_cast_fn(EGD_FLOAT, EGD_FLOAT, 0);
		ibgrp[i].in_tsize = sizeof(scaled_t);
		ibgrp[i].buff_tsize = sizeof(scaled_t);
		in_off += ibgrp[i].inlen;
	}
}


void init_buffer(scaled_t* buffer)
{
	unsigned int ich,is;

	for (ich=0; ich<orignumch; ich++) {
		for (is=0; is<NS; is++) {
			buffer[ich+is*orignumch] = (is % 17)+ich*3;
		}
	}
}


void copy_buffers(scaled_t* restrict out, scaled_t* restrict in, unsigned int ns)
{
	unsigned int i;

	out += inbuff_offset;
	for (i=0; i<ns; i++) {
		memcpy(out, in, orignumch*sizeof(scaled_t));
		out += innumch;
		in += orignumch;
	}
}

int main(int argc, char* argv[])
{
	int retval = 0;
	size_t len;
	unsigned int i;

	struct eegdev* dev;
	scaled_t* inbuffer, *origbuffer;
	char* ref, *test;
	struct mmarg_parser parser = {
		.optv = arg_options,
		.num_opt = MM_NELEM(arg_options),
		.execname = argv[0]
	};

	mmarg_parse(&parser, argc, argv);

	origbuffer = malloc(NS*orignumch*sizeof(scaled_t));
	inbuffer = malloc(NS*innumch*sizeof(scaled_t));
	init_buffer(origbuffer);
	copy_buffers(inbuffer, origbuffer, NS);

	dev = egdi_create_eegdev(&info);
	dev->inbuffgrp = malloc(NGRP*sizeof(*(dev->inbuffgrp)));
	dev->ngrp = NGRP;
	init_inbufgrp(dev->inbuffgrp, NGRP);
	dev->acquiring = 1;
	dev->buffer = malloc(NS*orignumch*sizeof(scaled_t));
	dev->strides = NULL;
	dev->arrconf = NULL;
	dev->in_samlen = innumch*sizeof(scaled_t);
	dev->buff_samlen = orignumch*sizeof(scaled_t);
	dev->buffsize = NPOINT*sizeof(scaled_t);
	dev->buff_ns = NS;

	i = 0;
	while (i<INNPOINT) {
		len = (i + chunklen < INNPOINT) ? chunklen : INNPOINT-i;
		dev->module.ci.update_ringbuffer(&dev->module, inbuffer + i, len*sizeof(scaled_t));
		i+=chunklen;
		dev->ns_read = dev->ns_written;
	}

	ref = (char*)origbuffer;
	test = (char*)dev->buffer;
	for (i=0; i<NS; i++) {
		if (memcmp(ref, test, dev->buff_samlen)) {
			fprintf(stderr, "mismatch at sample %i\n", i);
			retval = 1;
			break;
		}
		ref += dev->buff_samlen;
		test += dev->buff_samlen;
	}

	egd_destroy_eegdev(dev);
	free(origbuffer);
	free(inbuffer);

	return retval;
}
