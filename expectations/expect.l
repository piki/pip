%{
#include <stdlib.h>
void yyerror(const char *fmt, ...);
#include "parsetree.h"
#include "expect.tab.hh"
int yylno = 1;
static char string_buf[1024], *string_p;
%}

digit		[0-9]
letter	[A-Za-z_]

%x S_REGEX
%x S_STRING
%x S_COMMENT_BLOCK
%x S_COMMENT_LINE

%%

{digit}+												{ yylval.iValue = atoi(yytext); return INTEGER; }
"<="														return LE;
">="														return GE;
"=="														return EQ;
"!="														return NE;
"->"														return IMPLIES;
"&&"														return B_AND;
"||"														return B_OR;
[-+()=/*;:{},><!]								return *yytext;
[ \t]                         
\n															yylno++;
\.\.\.													return ELLIPSIS;
"expectation"										return EXPECTATION;
"fragment"											return FRAGMENT;
"repeat"												return REPEAT;
"reverse"												return REVERSE;
"message"												return MESSAGE;
"task"													return TASK;
"limit"													return LIMIT;
"split"													return SPLIT;
"branch"												return BRANCH;
"join"													return JOIN;
"notice"												return NOTICE;
"xor"														return XOR;
"call"													return CALL;
"between"												return BETWEEN;
"and"														return AND;
"assert"												return ASSERT;
"instances"											return INSTANCES;
"unique"												return UNIQUE;
"in"														return IN;
"during"												return DURING;
"any"														return ANY;
"average"												return AVERAGE;
"stddev"												return STDDEV;
"max"														return F_MAX;
\${letter}({letter}|{digit})*		{ yylval.symbol = symbol_lookup(yytext); return STRINGVAR; }
\@{letter}({letter}|{digit})* 	{ yylval.symbol = symbol_lookup(yytext); return PATHVAR; }
{letter}({letter}|{digit})*			{ yylval.symbol = symbol_lookup(yytext); return IDENTIFIER; }

\/\*														BEGIN S_COMMENT_BLOCK;
<S_COMMENT_BLOCK>\*\/						BEGIN 0;
<S_COMMENT_BLOCK>.	
<S_COMMENT_BLOCK>\n							yylno++;

\/\/														BEGIN S_COMMENT_LINE;
<S_COMMENT_LINE>\n							{ BEGIN 0; yylno++; }
<S_COMMENT_LINE>.

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
