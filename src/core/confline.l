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
%option yylineno reentrant never-interactive batch bison-bridge
%option noyywrap noyyalloc noyyrealloc noyyfree nounput noinput
%option prefix="cfl_"
%option header-file="confline.lex.h"
%option outfile="confline.lex.c"
%{
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include "confline.h"
#include "confline.tab.h"
%}

%%
%{
	struct cfldata* pp = yyextra;
%}

[a-zA-Z0-9_.+-/:]*	{
				if (strlen(yytext)>TOKEN_MAXLEN-1) 
					return -1;
			
				strcpy(pp->tokbuff[pp->itok], yytext);
				yylval->str = pp->tokbuff[pp->itok];
				pp->itok = (pp->itok+1)%NTOK;
			
				return WORD;
			}
\|		{return yytext[0];}
[ \t]+          /* ignore whitespace */;

%%


/**********************************************************
 * reimplement these functions to switch off the warnings *
 **********************************************************/
LOCAL_FN
void *cfl_alloc (yy_size_t  size , yyscan_t yyscanner)
{
	(void)yyscanner;
	return malloc(size);
}


LOCAL_FN
void *cfl_realloc  (void * ptr, yy_size_t  size , yyscan_t yyscanner)
{
	(void)yyscanner;
	return realloc(ptr, size);
}


LOCAL_FN
void cfl_free (void * ptr , yyscan_t yyscanner)
{
	(void)yyscanner;
	free(ptr);
}

