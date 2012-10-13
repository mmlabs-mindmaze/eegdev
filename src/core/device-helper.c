/*
    Copyright (C) 2011-2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
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
#include <stddef.h>
#include <eegdev-pluginapi.h>
#include <string.h>
#include "coreinternals.h"


static
int egdi_next_chindex(const struct egdi_chinfo* ch, int stype, int tind)
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


static
int egdi_in_offset(const struct egdi_chinfo* ch, int ind)
{
	int chind, offset = 0;

	for (chind=0; chind<ind; chind++) 
		offset += egd_get_data_size(ch[chind].si->dtype);

	return offset;
}


static
int split_chgroup(const struct egdi_chinfo* cha, const struct grpconf *grp,
		       struct selected_channels *sch)
{
	union gval sc = {.valdouble = 0.0};
	int ich, nxt=0, is = 0, stype = grp->sensortype, index = grp->index;
	int ti, bsc, to = grp->datatype;
	unsigned int i, offset, len = 0;
	unsigned int arr_offset = grp->arr_offset, nch = grp->nch;
	unsigned int tosize = egd_get_data_size(to);

	if (!nch)
		return 0;

	ich = egdi_next_chindex(cha, stype, index);
	offset = egdi_in_offset(cha, ich);
	ti = cha[ich].si->dtype;
	bsc = cha[ich].si->bsc;
	egdi_set_gval(&sc, to, cha[ich].si->scale);

	// Scan the whole channel group (if i == nch, we close the group)
	for (i = 0; i <= nch; i++) {
		if ( (i == nch)
		   || ((nxt = egdi_next_chindex(cha+ich, stype, 0)))
		   || (ti != cha[ich].si->dtype)) {
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
				sch[is].bsc = bsc;
				sch[is].sc = sc;
			}
			is++;
		   	ich += nxt;
			arr_offset += len * tosize;
			offset = (i!=nch) ? egdi_in_offset(cha, ich) : 0;
			ti = (i!=nch) ? cha[ich].si->dtype : 0;
			len = 0;
		}
		len++;
		ich++;
	}

	return is;
}


LOCAL_FN
int egdi_split_alloc_chgroups(struct eegdev* dev,
                              unsigned int ngrp, const struct grpconf* grp)
{
	unsigned int i, nsel = 0;
	struct devmodule* mdev = &dev->module;
	struct selected_channels* selch;

	// Compute the number of needed groups
	for (i=0; i<ngrp; i++)
		nsel += split_chgroup(dev->cap.chmap, grp+i, NULL);

	if (!(selch = mdev->ci.alloc_input_groups(mdev, nsel)))
		return -1;
	
	// Setup selch
	nsel = 0;
	for (i=0; i<ngrp; i++)
		nsel += split_chgroup(dev->cap.chmap, grp+i, selch+nsel);
		
	return 0;
}


LOCAL_FN
void egdi_default_fill_chinfo(const struct eegdev* dev, int stype,
	                      unsigned int ich, struct egdi_chinfo* info,
			      struct egdi_signal_info* si)
{
	int index = egdi_next_chindex(dev->cap.chmap, stype, ich);

	// Fill channel metadata
	info->label = dev->cap.chmap[index].label;
	info->stype = stype;
	if ( dev->cap.chmap[index].si)
		memcpy(si, dev->cap.chmap[index].si, sizeof(*si));
}

