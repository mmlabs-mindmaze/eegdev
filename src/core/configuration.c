/*
    Copyright (C) 2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)
    Nicolas Bourdaud <nicolas.bourdaud@gmail.com>

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

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "eegdev-pluginapi.h"
#include "configuration.h"

#define INCBUFSIZE	1024
#define INCSIZE		32

struct setting {
	const char* optname;
	const char* value;
};

struct mapping {
	const char* name;
	int start;
	int nch;
};


static
void dynarray_free(struct dynarray* ar)
{
	free(ar->array);
}


static
void dynarray_init(struct dynarray* ar, size_t eltsize, int incsize)
{
	ar->array = NULL;
	ar->num = ar->nmax = 0;
	ar->eltsize = eltsize;
	ar->incsize = incsize;
}


static
void dynarray_reinit(struct dynarray* ar)
{
	ar->num = 0;
}


static
int dynarray_push(struct dynarray* ar, const void* newelt)
{
	int nmax;
	void* newbuff, *elt;


	if (ar->num == ar->nmax) {
		nmax = ar->nmax + ar->incsize;
		newbuff = realloc(ar->array, nmax*ar->eltsize);
		if (!newbuff)
			return -1;
		ar->nmax = nmax;
		ar->array = newbuff;
	}

	elt = ((char*)ar->array) + ar->num*ar->eltsize;
	memcpy(elt, newelt, ar->eltsize);

	return ar->num++;
}


LOCAL_FN
void egdi_free_config(struct egdi_config* cf)
{
	struct strpage *curr, *next = cf->start;

	// Free all pages of strings
	while (next) {
		curr = next;
		next = curr->next;
		free(curr);
	}

	dynarray_free(&cf->ar_settings);
	dynarray_free(&cf->ar_channels);
	dynarray_free(&cf->ar_mappings);
}


LOCAL_FN
void egdi_init_config(struct egdi_config* cf)
{
	cf->start = NULL;
	cf->last = NULL;
	dynarray_init(&cf->ar_settings, sizeof(struct setting), INCSIZE);
	dynarray_init(&cf->ar_channels, sizeof(struct egdi_chinfo), INCSIZE);
	dynarray_init(&cf->ar_mappings, sizeof(struct mapping), INCSIZE);
}


LOCAL_FN
void egdi_reinit_config(struct egdi_config* cf)
{
	if (cf->start)
		cf->start->nused = 0;
	cf->last = cf->start;

	dynarray_reinit(&cf->ar_settings);
	dynarray_reinit(&cf->ar_channels);
	dynarray_reinit(&cf->ar_mappings);
}


static
const char* egdi_add_string(struct egdi_config* cf, const char* str)
{
	struct strpage** plast;
	char* str_inpage;
	size_t len = strlen(str)+1;

	if (len > STRBUFF_CHUNKSIZE) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	// Add a new page of string if the current one is too small (or does
	// not exist) to hold the new string
	if (!cf->last || (len + cf->last->nused > STRBUFF_CHUNKSIZE)) {
		plast = cf->last ? &cf->last->next : &cf->start;
		if (!*plast) {
			*plast = malloc(sizeof(*cf->last));
			if (!*plast)
				return NULL;
		}
		cf->last = *plast;
		cf->last->nused = 0;
		cf->last->next = NULL;
	}

	// happen the string on the current string page
	str_inpage = cf->last->data + cf->last->nused;
	strcpy(str_inpage, str);
	cf->last->nused += len;

	return str_inpage;
}


LOCAL_FN
int egdi_add_setting(struct egdi_config* cf,
                     const char* name, const char* value)
{
	struct setting set;

	// Store the strings into the configuration buffer
	set.optname = egdi_add_string(cf, name);
	set.value = egdi_add_string(cf, value);
	if (!set.optname || !set.value)
		return -1;

	return (dynarray_push(&cf->ar_settings, &set) > 0) ? 0 : -1;
}


LOCAL_FN
const char* egdi_get_setting_value(struct egdi_config* cf, const char* name)
{
	unsigned int i = cf->ar_settings.num;
	struct setting* settings = cf->ar_settings.array;

	// Search backward for the setting of the specified name: all prior
	// definitions of a setting are overriden by the latest definition
	while (i) {
		i--;
		if (!strcmp(settings[i].optname, name))
			return settings[i].value;
	}

	return NULL;
}


LOCAL_FN
int egdi_add_channel(struct egdi_config* cf, int stype, const char* label)
{
	struct egdi_chinfo ch = {.stype = stype};

	ch.label = egdi_add_string(cf, label);
	if (!ch.label)
		return -1;

	return (dynarray_push(&cf->ar_channels, &ch) > 0) ? 0 : -1;
}


LOCAL_FN
int egdi_start_mapping(struct egdi_config* cf, const char* name)
{
	struct mapping map = {.start = cf->ar_channels.num};

	map.name = egdi_add_string(cf, name);
	if (!map.name)
		return -1;

	return (dynarray_push(&cf->ar_mappings, &map) > 0) ? 0 : -1;
}


LOCAL_FN
void egdi_end_mapping(struct egdi_config* cf)
{
	struct mapping* mappings = cf->ar_mappings.array;
	int last = cf->ar_mappings.num;

	mappings[last].nch = cf->ar_channels.num - mappings[last].start;
}

