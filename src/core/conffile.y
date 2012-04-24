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
%name-prefix "cff_"
%defines "conffile.tab.h"
%parse-param { struct cfdata *pp }
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
#include "conffile.lex.h"
#include "conffile.h"
#define YYLEX_PARAM pp->scaninfo

static int yyerror(struct cfdata *pp, const char* s);
%}

/* declare tokens */
%token <str> WORD
%token EOL

%%
setlist: 
  | setlist setting
  | setlist EOL
;

setting: WORD '=' WORD EOL {egdi_add_setting(pp->cf, $1, $3);}

%%

static
int yyerror(struct cfdata *pp, const char *s)
{
	fprintf(stderr, "error while parsing (%s): %s\n", pp->fpath, s);
	return 0;
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
		if (errno == ENOENT) {
			errno = 0;
			return 0;
		}
		return -1;
	}
	
	// Initialize the lexer
	if (cff_lex_init_extra(&p, &p.scaninfo)) {
		fclose(fp);
		return -1;
	}

	// Run the parser on the opened file and close it afterwards
	cff_set_in(fp, p.scaninfo);
	ret = yyparse(&p);
	cff_lex_destroy(p.scaninfo);
	fclose(fp);

	return ret;
}

