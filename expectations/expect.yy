%{
#include "parsetree.h"
%}
%union {
	int iValue;
	char *sValue;
	Symbol *symbol;
	Node *nPtr;
	ListNode *nList;
};

// keywords
%token EXPECTATION
%token FRAGMENT
%token REPEAT
%token REVERSE
%token JOIN
%token BRANCH
%token SPLIT
%token XOR
%token CALL
%token BETWEEN
%token AND
%token ASSERT
%token INSTANCES
%token UNIQUE
%token DURING
%token ANY
%token AVERAGE
%token STDDEV
%token F_MAX
// operators
%token GE
%token LE
%token EQ
%token NE
%token B_AND
%token B_OR
%token IMPLIES
%token IN
// operations
%token MESSAGE
%token TASK
%token NOTICE
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
%type <nList> ident_list limit_list statement_list thread_list xor_list
%type <nPtr> pathdecl statement thread
%type <nPtr> thread_set repeat limit range repeat_range path_expr
%type <nPtr> string_expr event xor task assert assertdecl bool_expr
%type <nPtr> int_expr window float_expr string_literal

%left B_AND B_OR IMPLIES
%left '<' '>' LE GE EQ NE
%left '+' '-'
%left '*' '/'
%nonassoc '!'
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
		program pathdecl													{ add_recognizer($2); delete $2; }
		| program assertdecl											{ add_assert($2); delete $2; }
		|
		;

pathdecl:
		EXPECTATION IDENTIFIER '{' statement_list '}'	{ $$ = opr(EXPECTATION, 2, id($2), $4); }
		| FRAGMENT IDENTIFIER '{' statement_list '}'	{ $$ = opr(FRAGMENT, 2, id($2), $4); }
		;

ident_list:
		ident_list ',' IDENTIFIER									{ $$ = $1; ($$)->add(id($3)); }
		| IDENTIFIER															{ $$ = new ListNode; ($$)->add(id($1)); }
		;

statement_list:
		statement_list statement									{ $$ = $1; ($$)->add($2); }
		|																					{ $$ = new ListNode; }
		;
		
statement:
		REVERSE '(' path_expr ')' ';'							{ $$ = opr(REVERSE, 1, $3); }
		| MESSAGE '(' ')' ';'											{ $$ = opr(MESSAGE, 0); }
		| CALL '(' IDENTIFIER ')' ';'							{ $$ = opr(CALL, 1, id($3)); }
		| event ';'
		| task limit_list ';'											{ $$ = opr(TASK, 3, $1, $2, NULL); }
		| task limit_list '{' statement_list '}'	{ $$ = opr(TASK, 3, $1, $2, $4); }
		| string_expr
		| path_expr
		| SPLIT '{' thread_list '}' JOIN '(' thread_set ')' ';'			{ $$ = opr(SPLIT, 2, $3, $7); }
		| XOR '{' xor_list '}'										{ $$ = opr(XOR, 1, $3); }
		| '{' statement_list '}'									{ $$ = $2; }
		| error ';'																{ $$ = NULL; }
		;

xor_list:
		xor_list xor															{ $$ = $1; ($$)->add($2); }
		|																					{ $$ = new ListNode; }
		;

xor:
		BRANCH ':' statement_list									{	$$ = opr(BRANCH, 1, $3); }
		;

thread_list:
		thread_list thread												{ $$ = $1; ($$)->add($2); }
		|																					{ $$ = new ListNode; }
		;

thread:
		BRANCH IDENTIFIER ':' statement_list			{	$$ = opr(BRANCH, 2, id($2), $4); }
		;

thread_set:
		ident_list																{ $$ = $1; }
		| INTEGER																	{ $$ = new IntNode($1); }
		;

repeat:
		REPEAT repeat_range statement							{ $$ = opr(REPEAT, 2, $2, $3); }
		;

limit_list:
		limit_list limit													{ $$ = $1; ($$)->add($2); }
		|																					{ $$ = new ListNode; }
		;

limit:
		LIMIT '(' IDENTIFIER ',' range ')'				{ $$ = opr(LIMIT, 2, id($3), $5); }
		;

range:
		'{' float_expr ELLIPSIS float_expr '}'		{ $$ = opr(RANGE, 2, $2, $4); }
		| '{' float_expr ELLIPSIS '}'							{ $$ = opr(RANGE, 2, $2, NULL); }
		;

repeat_range:
		INTEGER																		{ $$ = opr(RANGE, 2, new IntNode($1), new IntNode($1)); }
		| BETWEEN INTEGER AND INTEGER							{ $$ = opr(RANGE, 2, new IntNode($2), new IntNode($4)); }
		;

path_expr:
		repeat
		| PATHVAR																	{ $$ = id($1); }
		| PATHVAR '=' path_expr										{ $$ = opr('=', 2, id($1), $3); }
		;

task:
		TASK '(' string_expr ',' string_expr ')'	{ $$ = opr(TASK, 2, $3, $5); }
		;

event:
		NOTICE '(' string_expr ',' string_expr ')'	{ $$ = opr(NOTICE, 2, $3, $5); }
		;

string_literal:
		STRING																		{ $$ = new StringNode(false, $1); }
		| REGEX																		{ $$ = new StringNode(true, $1); }
		;

string_expr:
		string_literal
		| STRINGVAR																{ $$ = id($1); }
		| STRINGVAR '=' string_expr								{ $$ = opr('=', 2, id($1), $3); }
		;

assertdecl:
		DURING '(' window ')' assert							{ $$ = opr(DURING, 2, $3, $5); }
		| assert
		;

window:
		INTEGER ',' INTEGER												{ $$ = opr(RANGE, 2, new IntNode($1), new IntNode($3)); }
		| ANY INTEGER															{ $$ = opr(ANY, 1, new IntNode($2)); }
		;

assert:
		ASSERT '(' bool_expr ')'									{ $$ = opr(ASSERT, 1, $3); }
		;

bool_expr:
		bool_expr B_AND bool_expr									{ $$ = opr(B_AND, 2, $1, $3); }
		| bool_expr B_OR bool_expr								{ $$ = opr(B_OR, 2, $1, $3); }
		| bool_expr IMPLIES bool_expr							{ $$ = opr(IMPLIES, 2, $1, $3); }
		| '!' bool_expr														{ $$ = opr('!', 1, $2); }
		| '(' bool_expr ')'												{ $$ = $2; }
		| float_expr IN range											{ $$ = opr(IN, 2, $1, $3); }
		| float_expr '<' float_expr								{ $$ = opr('<', 2, $1, $3); }
		| float_expr '>' float_expr								{ $$ = opr('>', 2, $1, $3); }
		| float_expr LE float_expr								{ $$ = opr(LE, 2, $1, $3); }
		| float_expr GE float_expr								{ $$ = opr(GE, 2, $1, $3); }
		| float_expr EQ float_expr								{ $$ = opr(EQ, 2, $1, $3); }
		| float_expr NE float_expr								{ $$ = opr(NE, 2, $1, $3); }
		;

float_expr:
		int_expr
		| AVERAGE '(' IDENTIFIER ',' IDENTIFIER ')'				{ $$ = opr(AVERAGE, 2, id($3), id($5)); }
		| AVERAGE '(' IDENTIFIER ',' string_literal ')'		{ $$ = opr(AVERAGE, 2, id($3), $5); }
		| STDDEV '(' IDENTIFIER ',' IDENTIFIER ')'				{ $$ = opr(STDDEV, 2, id($3), id($5)); }
		| STDDEV '(' IDENTIFIER ',' string_literal ')'		{ $$ = opr(STDDEV, 2, id($3), $5); }
		| F_MAX '(' IDENTIFIER ',' IDENTIFIER ')'					{ $$ = opr(F_MAX, 2, id($3), id($5)); }
		| F_MAX '(' IDENTIFIER ',' string_literal ')'			{ $$ = opr(F_MAX, 2, id($3), $5); }
		;

int_expr:
		INTEGER																		{ $$ = new IntNode($1); }
		| INSTANCES '(' IDENTIFIER ')'						{ $$ = opr(INSTANCES, 1, id($3)); }
		| UNIQUE '(' IDENTIFIER ')'								{ $$ = opr(UNIQUE, 1, id($3)); }
		| UNIQUE '(' STRINGVAR ')'								{ $$ = opr(UNIQUE, 1, id($3)); }
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

void expect_parse(const char *filename) {
	yyin = fopen(filename, "r");
	if (!yyin) {
		perror(filename);
		exit(1);
	}
	yyparse();
	fclose(yyin);
}
