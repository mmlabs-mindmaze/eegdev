/*
    Copyright (C) 2012  EPFL (Ecole Polytechnique Fédérale de Lausanne)
    Copyright (C) 2012  Nicolas Bourdaud <nicolas.bourdaud@gmail.com>
    Laboratory CNBI (Chair in Non-Invasive Brain-Machine Interface)

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
%define api.pure
%no-lines
%name-prefix "cf"
%defines "configuration.tab.h"
%parse-param { struct cfdata *pp }
%lex-param { yyscan_t cfscaninfo }
%{
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include "eegdev.h"
#include "eegdev-pluginapi.h"
#include "configuration.h"
#include "confparser.h"
#ifndef YY_TYPEDEF_YY_SCANNER_T
#define YY_TYPEDEF_YY_SCANNER_T
typedef void* yyscan_t;
#endif
#include "configuration.h"
%}

%union value {
	const char* str;
	int integer;
}

%{
#include "configuration.lex.h"
#define cfscaninfo pp->scaninfo

static int yyerror(struct cfdata *pp, const char* s);
static int egdi_add_setting(struct egdi_config*, const char*, const char*);
static int egdi_add_channel(struct egdi_config*, int, const char*);
static int egdi_start_mapping(struct egdi_config*, const char*);
static void egdi_end_mapping(struct egdi_config*);

%}

/* declare tokens */
%token <str> WORD
%token EOL DEFMAP ENDMAP

%%
conflist:  WORD {
			egdi_add_setting(pp->cf, "device", $1);
			cfd_pop_string(pp, 1);
		}
  | setlist {}
  ;

setlist: 
  | setting {}
  | setlist EOL {}
  | setlist EOL setting {}
  | setlist start_mapping chlist end_mapping {}
  ;

setting: WORD '=' WORD {
				egdi_add_setting(pp->cf, $1, $3);
				cfd_pop_string(pp, 2);
                       }
  ;

start_mapping: DEFMAP WORD EOL {
				egdi_start_mapping(pp->cf, $2);
				cfd_pop_string(pp, 1);
			       }
  ;

end_mapping: ENDMAP EOL { egdi_end_mapping(pp->cf); }
  ;

chlist:
  | chlist WORD WORD EOL {
				int type = egd_sensor_type($2);
				egdi_add_channel(pp->cf, type,  $3);
				cfd_pop_string(pp, 2);
			 }
 ;
%%

static
int yyerror(struct cfdata *pp, const char *s)
{
	fprintf(stderr, "error while parsing %s: %s\n",
	                pp->fpath ? "'%s'" : "device options string", s);
	return 0;
}


/****************************************************
 *            Configuration tree internals          *
 ****************************************************/
#define STRBUFF_CHUNKSIZE	4000

#define INCBUFSIZE	1024
#define INCSIZE		32

struct strpage {
	struct strpage* next;
	unsigned int nused;
	char data[STRBUFF_CHUNKSIZE];
};


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


static
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


static
int egdi_add_channel(struct egdi_config* cf, int stype, const char* label)
{
	struct egdi_chinfo ch = {.stype = stype};

	ch.label = egdi_add_string(cf, label);
	if (!ch.label)
		return -1;

	return (dynarray_push(&cf->ar_channels, &ch) > 0) ? 0 : -1;
}


static
int egdi_start_mapping(struct egdi_config* cf, const char* name)
{
	struct mapping map = {.start = cf->ar_channels.num};

	map.name = egdi_add_string(cf, name);
	if (!map.name)
		return -1;

	return (dynarray_push(&cf->ar_mappings, &map) > 0) ? 0 : -1;
}


static
void egdi_end_mapping(struct egdi_config* cf)
{
	struct mapping* mappings = cf->ar_mappings.array;
	int last = cf->ar_mappings.num - 1;

	mappings[last].nch = cf->ar_channels.num - mappings[last].start;
}


/****************************************************
 *             Configuration function exported      *
 ****************************************************/
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
struct egdi_chinfo* egdi_get_cfmapping(struct egdi_config* cf,
                                       const char* name, int* nch)
{
	unsigned int i = cf->ar_mappings.num;
	struct mapping *mappings = cf->ar_mappings.array;
	struct egdi_chinfo *channels = cf->ar_channels.array;

	// Search backward for the setting of the specified name: all prior
	// definitions of a setting are overriden by the latest definition
	while (i) {
		i--;
		if (!strcmp(mappings[i].name, name)) {
			*nch = mappings[i].nch;
			return channels + mappings[i].start;
		}
	}

	return NULL;
}


LOCAL_FN
int egdi_parse_conffile(struct egdi_config* cf, const char* filename)
{
	struct cfdata p = { .cf = cf, .fpath = filename, .scaninfo = NULL };
	FILE* fp = NULL;
	int ret;

	// Open the configuration
	// Failing because the file does not exist is NOT an error
	if (!(fp = fopen(filename, "r"))) {
		if (errno == ENOENT || errno == EACCES) {
			errno = 0;
			return 0;
		}
		return -1;
	}

	// Initialize the lexer
	if (cflex_init_extra(&p, &p.scaninfo)) {
		fclose(fp);
		return -1;
	}

	// Run the parser on the opened file and close it afterwards
	cfset_in(fp, p.scaninfo);
	ret = yyparse(&p);
	cflex_destroy(p.scaninfo);
	fclose(fp);

	return ret;
}


LOCAL_FN
int egdi_parse_confline(struct egdi_config* cf, const char* confstr)
{
	struct cfdata p = { .cf = cf, .scaninfo = NULL };
	YY_BUFFER_STATE buf;
	int ret;

	if (!confstr)
		return 0;

	// Initialize the lexer
	if (cflex_init_extra(&p, &p.scaninfo))
		return -1;

	// Set the input of the scanner to the configuration string
	if (!(buf = cf_scan_string(confstr, p.scaninfo))) {
		cflex_destroy(p.scaninfo);
		return -1;
	}

	// Run the parser on the string and clean everything
	ret = yyparse(&p);
	cf_delete_buffer(buf, p.scaninfo);
	cflex_destroy(p.scaninfo);

	return ret;
}

