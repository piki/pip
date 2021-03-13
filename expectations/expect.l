/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

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

"<="														return LE;
">="														return GE;
"=="														return EQ;
"!="														return NE;
"->"														return IMPLIES;
"&&"														return B_AND;
"||"														return B_OR;
"\*\*"													return F_POW;
[-+()=/*;:{},><!]								return *yytext;
[ \t]                         
\n															yylno++;
"validator"											return VALIDATOR;
"invalidator"										return INVALIDATOR;
"fragment"											return FRAGMENT;
"recognizer"										return RECOGNIZER;
"level"													return LEVEL;
"repeat"												return REPEAT;
"send"													return SEND;
"recv"													return RECV;
"task"													return TASK;
"thread"												return THREAD;
"limit"													return LIMIT;
"branch"												return BRANCH;
"notice"												return NOTICE;
"xor"														return XOR;
"maybe"													return MAYBE;
"call"													return CALL;
"future"												return FUTURE;
"done"													return DONE;
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
"min"														return F_MIN;
"log"														return LOG;
"ln"														return LOGN;
"sqrt"													return SQRT;
"exp"														return EXP;
\${letter}({letter}|{digit})*		{ yylval.sValue = strdup(yytext); return VARIABLE; }
{letter}({letter}|{digit})*			{ yylval.sValue = strdup(yytext); return IDENTIFIER; }
{digit}+												{ yylval.iValue = atoi(yytext); return INTEGER; }
{digit}+\.{digit}*							{ yylval.fValue = atof(yytext); return FLOAT; }

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

int yywrap(void) {
	if (0) yyunput(0,0);  /* quell the "unused" warning */
	return 1;
}
