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
#ifndef CONFFILE_H
#define CONFFILE_H

#include <string.h>

#ifndef YY_TYPEDEF_YY_SCANNER_T
#define YY_TYPEDEF_YY_SCANNER_T
typedef void* yyscan_t;
#endif

#define NTOK		10
#define TOKEN_MAXLEN	64
struct cfdata {
	struct egdi_config* cf;
	const char* fpath;
	char tokbuff[NTOK][TOKEN_MAXLEN];
	int itok;
	yyscan_t scaninfo;      /* scanner context */
};


static inline
const char* cfd_push_string(struct cfdata* pp, const char* str)
{
			
	if ((strlen(str)>TOKEN_MAXLEN-1) || (pp->itok == NTOK))
		return NULL;
	
	strcpy(pp->tokbuff[pp->itok], str);
	return pp->tokbuff[pp->itok++];
}


static inline
void cfd_pop_string(struct cfdata* pp, int numel)
{
	pp->itok -= numel;
}


#endif //CONFFILE_H
