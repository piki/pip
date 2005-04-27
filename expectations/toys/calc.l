%{
#include <readline/readline.h>
#include <readline/history.h>
#include <stdlib.h>
void yyerror(char*);
#include "calc.tab.h"
#define YY_INPUT(buf,result,max_size) rl_get(buf, &result, max_size)
static void rl_get(char *buf, int *result, int max_size) {
	static char *rl_buf, *rl_p;
	static int rl_len = 0;
	int bytes;

	if (rl_len == 0) {
		rl_p = rl_buf = readline("> ");
		if (!rl_buf) { *result = 0; return; }
		rl_len = strlen(rl_buf);
		if (rl_len > 0) add_history(rl_buf);
		rl_buf[rl_len++] = '\n';    /* replace 0-terminator with a newline */
	}

	bytes = rl_len > max_size ? max_size : rl_len;
	memcpy(buf, rl_buf, bytes);
	*result = bytes;
	rl_p += bytes;
	rl_len -= bytes;

	if (rl_len == 0)
		free(rl_buf);
}
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
