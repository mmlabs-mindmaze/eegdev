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

#include <errno.h>
#include <unistd.h>
#include <eegdev-common.h>
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


/**************************************************************************
 *                             Group splitting                            *
 **************************************************************************/
LOCAL_FN
int egdi_next_chindex(const struct egdich* ch, unsigned int stype, int tind)
{
	int chind, itype = 0;

	for (chind = 0;; chind++) {
		if (ch[chind].stype == stype) {
			if (itype++ == tind)
				break;
		}
	}

	return chind;
}


LOCAL_FN
int egdi_in_offset(const struct egdich* ch, int ind)
{
	int chind, offset = 0;

	for (chind=0; chind<ind; chind++) 
		offset += egd_get_data_size(ch[chind].dtype);

	return offset;
}


static
int split_chgroup(const struct egdich* cha, const struct grpconf *grp,
		       struct selected_channels *sch)
{
	int ich, nxt=0, is = 0, stype = grp->sensortype, index = grp->index;
	unsigned int i, offset, ti, to = grp->datatype, len = 0;
	unsigned int arr_offset = grp->arr_offset, nch = grp->nch;
	unsigned int tosize = egd_get_data_size(to);

	if (!nch)
		return 0;

	ich = egdi_next_chindex(cha, stype, index);
	offset = egdi_in_offset(cha, ich);
	ti = cha[ich].dtype;

	// Scan the whole channel group (if i == nch, we close the group)
	for (i = 0; i <= nch; i++) {
		if ( (i == nch)
		   || ((nxt = egdi_next_chindex(cha+ich, stype, 0)))
		   || (ti != cha[ich].dtype)) {
		   	// Don't add empty group
		   	if (!len)
				break;
			if (sch) {
				sch[is].in_offset = offset;
				sch[is].inlen = len * egd_get_data_size(ti);
				sch[is].typein = ti;
				sch[is].typeout = to;
				sch[is].arr_offset = arr_offset;
				sch[is].iarray = grp->iarray;
			}
			is++;
		   	ich += nxt;
			arr_offset += len * tosize;
			offset = egdi_in_offset(cha, ich);
			ti = cha[ich].dtype;
			len = 0;
		}
		len++;
		ich++;
	}

	return is;
}


LOCAL_FN
int egdi_split_alloc_chgroups(struct eegdev* dev,
                              const struct egdich* channels,
                              unsigned int ngrp, const struct grpconf* grp)
{
	unsigned int i, nsel = 0;
	struct selected_channels* selch;

	// Compute the number of needed groups
	for (i=0; i<ngrp; i++)
		nsel += split_chgroup(channels, grp+i, NULL);

	if (!(selch = dev->ci.alloc_input_groups(dev, nsel)))
		return -1;
	
	// Setup selch
	nsel = 0;
	for (i=0; i<ngrp; i++)
		nsel += split_chgroup(channels, grp+i, selch+nsel);
		
	return nsel;
}


