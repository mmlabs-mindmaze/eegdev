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
%define api.pure
%name-prefix "cfl_"
%defines "confline.tab.h"
%parse-param { struct cfldata *pp }
%{
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include "configuration.h"
%}

%union value {
	const char* str;
}

%{
#include "confline.lex.h"
#include "confline.h"
#define YYLEX_PARAM pp->scaninfo

static int yyerror(struct cfldata *pp, const char* s);
%}

/* declare tokens */
%token <str> WORD

%%
setlist:
  | device
  | device '|' optlist

device:  WORD {egdi_add_setting(pp->cf, "device", $1);}
;

optlist: setting
  | optlist '|' setting
;

setting: WORD '|' WORD {egdi_add_setting(pp->cf, $1, $3);}

%%

static
int yyerror(struct cfldata *pp, const char *s)
{
	(void) pp;
	fprintf(stderr, "error: %s\n", s);
	return 0;
}


LOCAL_FN
int egdi_parse_confline(struct egdi_config* cf, const char* confstr)
{
	struct cfldata p = { .cf = cf, .scaninfo = NULL };
	YY_BUFFER_STATE buf;
	int ret;

	if (!confstr)
		return 0;

	// Initialize the lexer
	if (cfl_lex_init_extra(&p, &p.scaninfo))
		return -1;

	// Set the input of the scanner to the configuration string
	if (!(buf = cfl__scan_string(confstr, p.scaninfo))) {
		cfl_lex_destroy(p.scaninfo);
		return -1;
	}

	// Run the parser on the string and clean everything
	ret = yyparse(&p);
	cfl__delete_buffer(buf, p.scaninfo);
	cfl_lex_destroy(p.scaninfo);

	return ret;
}

