%{
#include <stdlib.h>
void yyerror(const char *fmt, ...);
#include "parsetree.h"
#include "y.tab.h"
int yylno = 1;
static char string_buf[1024], *string_p;
%}

digit		[0-9]
letter	[A-Za-z_]

%x S_REGEX
%x S_STRING

%%

{digit}+												{ yylval.iValue = atoi(yytext); return INTEGER; }
[-+()=/*;:{},]									return *yytext;
[ \t]                         
\n															yylno++;
\.\.\.													return ELLIPSIS;
"path"													return PATH;
"repeat"												return REPEAT;
"reverse"												return REVERSE;
"message"												return MESSAGE;
"task"													return TASK;
"limit"													return LIMIT;
"split"													return SPLIT;
"branch"												return BRANCH;
"join"													return JOIN;
"event"													return EVENT;
"xor"														return XOR;
"call"													return CALL;
\${letter}({letter}|{digit})*		{ yylval.symbol = symbol_lookup(yytext); return STRINGVAR; }
\@{letter}({letter}|{digit})* 	{ yylval.symbol = symbol_lookup(yytext); return PATHVAR; }
{letter}({letter}|{digit})*			{ yylval.symbol = symbol_lookup(yytext); return IDENTIFIER; }

\"															{ BEGIN S_STRING; string_p = string_buf; }
<S_STRING>\\n										*string_p++ = '\n';
<S_STRING>\\t										*string_p++ = '\t';
<S_STRING>\\\\									*string_p++ = '\\';
<S_STRING>\\\"									*string_p++ = '"';
<S_STRING>\"										{ BEGIN 0; *string_p = '\0'; yylval.sValue = string_buf; return STRING; }
<S_STRING>\n										{ yyerror("newline in string"); }
<S_STRING>.	                  	*string_p++ = *yytext;
                              
m\/															{ BEGIN S_REGEX; string_p = string_buf; }
<S_REGEX>\\\\	                	*string_p++ = '\\';
<S_REGEX>\\\/	                	*string_p++ = '/';
<S_REGEX>\/											{ BEGIN 0; *string_p = '\0'; yylval.sValue
= string_buf; return REGEX; }
<S_REGEX>.                    	*string_p++ = *yytext;

.																yyerror("invalid character: %c", *yytext);

%%

int yywrap(void) { return 1; }
