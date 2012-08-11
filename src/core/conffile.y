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
	int integer;
}

%{
#include "eegdev.h"
#include "confparser.h"
#include "conffile.lex.h"
#define YYLEX_PARAM pp->scaninfo

static int yyerror(struct cfdata *pp, const char* s);
%}

/* declare tokens */
%token <str> WORD
%token EOL DEFMAP ENDMAP

%%
setlist: 
  | setlist setting
  | setlist start_mapping chlist end_mapping
  | setlist EOL
;

setting: WORD '=' WORD EOL {
				egdi_add_setting(pp->cf, $1, $3);
				cfd_pop_string(pp, 2);
                           }

start_mapping: DEFMAP WORD EOL {
				egdi_start_mapping(pp->cf, $2);
				cfd_pop_string(pp, 1);
			       }

end_mapping: ENDMAP EOL { egdi_end_mapping(pp->cf); }

chlist:
  | chlist WORD WORD EOL {
				int type = egd_sensor_type($2);
				egdi_add_channel(pp->cf, type,  $3);
				cfd_pop_string(pp, 2);
			 }
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

