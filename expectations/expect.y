%{
#include "parsetree.h"
%}
%union {
	int iValue;
	char *sValue;
	Symbol *symbol;
	Node *nPtr;
};

// keywords
%token PATH
%token PATH_UNION
%token REPEAT
%token REVERSE
%token JOIN
%token BRANCH
%token SPLIT
%token XOR
%token CALL
// operations
%token MESSAGE
%token TASK
%token EVENT
%token LIMIT
// tokens
%token ELLIPSIS
%token RANGE
%token <sValue> STRING
%token <sValue> REGEX
%token <iValue> INTEGER
%token <symbol> IDENTIFIER
%token <symbol> STRINGVAR
%token <symbol> PATHVAR
%type <nPtr> pathdecl ident_list statement statement_list thread_list thread
%type <nPtr> thread_set repeat limit_list limit range path_expr task
%type <nPtr> string_expr event xor_list xor

%left '+' '-'
%left '*' '/'
%{
#include <stdarg.h>
#include <stdio.h>
void yyerror(const char *fmt, ...);
int yylex(void);
extern int yylno;
extern FILE *yyin;
char *filename;
%}

%%

program:
		program pathdecl													{ print_tree($2, 0); }
		|
		;

pathdecl:
		PATH IDENTIFIER '{' statement_list '}'	{ $$ = opr(PATH, 2, id($2), $4); }
		;

ident_list:
		ident_list ',' IDENTIFIER									{ $$ = opr(',', 2, $1, id($3)); }
		| IDENTIFIER															{ $$ = id($1); }
		;

statement_list:
		statement_list statement									{ $$ = opr(';', 2, $1, $2); }
		|																					{ $$ = NULL; }
		;
		
statement:
		REVERSE '(' path_expr ')' ';'							{ $$ = opr(REVERSE, 1, $3); }
		| MESSAGE '(' ')' ';'											{ $$ = opr(MESSAGE, 0); }
		| CALL '(' IDENTIFIER ')' ';'							{ $$ = opr(CALL, 1, $3); }
		| event ';'
		| task limit_list ';'											{ $$ = opr(TASK, 3, $1, $2, NULL); }
		| task limit_list '{' statement_list '}'	{ $$ = opr(TASK, 3, $1, $2, $4); }
		| string_expr
		| path_expr
		| SPLIT '{' thread_list '}' JOIN '(' thread_set ')' ';'		{ $$ = opr(SPLIT, 2, $3, $7); }
		| XOR '{' xor_list '}'										{ $$ = opr(XOR, 1, $3); }
		| '{' statement_list '}'									{ $$ = $2; }
		| error ';'																{ $$ = NULL; }
		;

xor_list:
		xor_list xor															{ $$ = opr(',', 2, $1, $2); }
		|																					{ $$ = NULL; }
		;

xor:
		BRANCH ':' statement_list									{	$$ = opr(BRANCH, 1, $3); }
		;

thread_list:
		thread_list thread												{ $$ = opr(',', 2, $1, $2); }
		|																					{ $$ = NULL; }
		;

thread:
		BRANCH IDENTIFIER ':' statement_list			{	$$ = opr(BRANCH, 2, id($2), $4); }
		;

thread_set:
		ident_list
		| INTEGER																	{ $$ = constant_int($1); }
		;

repeat:
		REPEAT range statement										{ $$ = opr(REPEAT, 2, $2, $3); }
		;

limit_list:
		limit_list limit													{ $$ = opr(',', 2, $1, $2); }
		|																					{ $$ = NULL; }
		;

limit:
		LIMIT '(' IDENTIFIER ',' range ')'				{ $$ = opr(LIMIT, 2, id($3), $5); }
		;

range:
		'{' INTEGER '}'														{ $$ = opr(RANGE, 2, constant_int($2), constant_int($2)); }
		| '{' INTEGER ELLIPSIS INTEGER '}'							{ $$ = opr(RANGE, 2, constant_int($2), constant_int($4)); }
		| '{' ELLIPSIS INTEGER '}'											{ $$ = opr(RANGE, 2, constant_int(0),  constant_int($3)); }
		| '{' INTEGER ELLIPSIS '}'											{ $$ = opr(RANGE, 2, constant_int($2), constant_int(RANGE_INF)); }
		;

path_expr:
		repeat
		| PATHVAR																	{ $$ = id($1); }
		| PATHVAR '=' path_expr										{ $$ = opr('=', 2, id($1), $3); }
		;

task:
		TASK '(' string_expr ',' string_expr ')'			{ $$ = opr(TASK, 2, $3, $5); }
		;

event:
		EVENT '(' string_expr ')'									{ $$ = opr(EVENT, 1, $3); }
		;

string_expr:
		STRING																		{ $$ = constant_string($1); }
		| REGEX																		{ $$ = constant_regex($1); }
		| STRINGVAR																{ $$ = id($1); }
		| STRINGVAR '=' string_expr								{ $$ = opr('=', 2, id($1), $3); }
		;

%%

void yyerror(const char *fmt, ...) {
	va_list args;
	fprintf(stderr, "%s:%d: ", filename, yylno);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
}

int main(int argc, char **argv) {
	symbols_init();
	if (argc > 1) {
		filename = argv[1];
		yyin = fopen(filename, "r");
		if (!yyin) {
			perror(filename);
			return 1;
		}
		yyparse();
		fclose(yyin);
	}
	else {
		filename = "<stdin>";
		yyparse();
	}
	return 0;
}
