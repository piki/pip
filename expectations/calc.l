%{
#include <stdlib.h>
void yyerror(char*);
#include "y.tab.h"
%}

digit		[0-9]
letter	[a-z]

%%

"sin"				return SIN;
"cos"				return COS;
"tan"				return TAN;
"sqrt"			return SQRT;
"pi"				return PI;
{letter}		{ yylval.iValue = *yytext - 'a'; return VARIABLE; }
{digit}+(\.{digit}*)?	{ yylval.fValue = atof(yytext); return FLOAT; }
[-+()=/*^_\n]	return *yytext;
[ \t]
.				yyerror("invalid character");

%%

int yywrap(void) { return 1; }
