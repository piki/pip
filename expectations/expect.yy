%{
#include "parsetree.h"
#include "aggregates.h"
#include "exptree.h"
%}
%union {
	int iValue;
	float fValue;
	char *sValue;
	Node *nPtr;
	ListNode *nList;
};

// keywords
%token VALIDATOR
%token RECOGNIZER
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
%token F_MIN
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
%token THREAD
%token NOTICE
%token LIMIT
// tokens
%token RANGE
%token <sValue> STRING
%token <sValue> REGEX
%token <iValue> INTEGER
%token <fValue> FLOAT
%token <sValue> IDENTIFIER
%token <sValue> STRINGVAR
%token <sValue> PATHVAR
%type <nList> branch_list limit_list statement_list thread_list xor_list
%type <nPtr> statement thread
%type <nPtr> branch_set repeat limit limit_range limit_spec repeat_range path_expr
%type <nPtr> string_expr event xor task assert assertdecl bool_expr
%type <nPtr> int_expr float_expr window string_literal unit_qty count_range

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
char *yyfilename;
bool yy_success = true;
%}

%%

program:
		program pathdecl
		| program assertdecl											{ if ($2) add_aggregate(new Aggregate($2)); }
		|
		;

pathdecl:
		VALIDATOR IDENTIFIER '{' statement_list '}' {
			if (yy_success) add_recognizer(new Recognizer(idcge($2,RECOGNIZER), (ListNode*)$4, true, true));
			delete $4;
		}
		| RECOGNIZER IDENTIFIER '{' statement_list '}' {
			if (yy_success) add_recognizer(new Recognizer(idcge($2,RECOGNIZER), (ListNode*)$4, true, false));
			delete $4;
		}
		| FRAGMENT VALIDATOR IDENTIFIER '{' statement_list '}' {
			if (yy_success) add_recognizer(new Recognizer(idcge($3,RECOGNIZER), (ListNode*)$5, false, true));
			delete $5;
		}
		| FRAGMENT RECOGNIZER IDENTIFIER '{' statement_list '}' {
			if (yy_success) add_recognizer(new Recognizer(idcge($3,RECOGNIZER), (ListNode*)$5, false, false));
			delete $5;
		}
		;

branch_list:
		branch_list ',' IDENTIFIER								{ $$ = $1; ($$)->add(idf($3,BRANCH)); }
		| IDENTIFIER															{ $$ = new ListNode; ($$)->add(idf($1,BRANCH)); }
		;

statement_list:
		statement_list statement									{ $$ = $1; ($$)->add($2); }
		|																					{ $$ = new ListNode; }
		;
		
statement:
		REVERSE '(' path_expr ')' ';'							{ $$ = opr(REVERSE, 1, $3); }
		| MESSAGE '(' ')' limit_list ';'					{ $$ = opr(MESSAGE, 1, $4); }
		| CALL '(' IDENTIFIER ')' ';'							{ $$ = opr(CALL, 1, idf($3,RECOGNIZER)); }
		| event ';'
		| task limit_list ';'											{ $$ = opr(TASK, 3, $1, $2, NULL); }
		| task limit_list '{' statement_list '}'	{ $$ = opr(TASK, 3, $1, $2, $4); }
		| THREAD ';'															{ $$ = opr(THREAD, 1, NULL); }
		| THREAD '{' statement_list '}'						{ $$ = opr(THREAD, 1, $3); }
		| string_expr
		| path_expr
		| SPLIT '{' thread_list '}' JOIN '(' ANY branch_set ')' ';'			{ $$ = opr(SPLIT, 2, $3, $8); }
		| XOR '{' xor_list '}'										{ $$ = opr(XOR, 1, $3); }
		| limit ';'
		| '{' statement_list '}'									{ $$ = $2; }
		| error ';'																{ $$ = NULL; }
		;

xor_list:
		xor_list xor															{ $$ = $1; ($$)->add($2); }
		|																					{ $$ = new ListNode; }
		;

xor:
		BRANCH ':' statement_list									{	$$ = opr(BRANCH, 2, NULL, $3); }
		;

thread_list:
		thread_list thread												{ $$ = $1; ($$)->add($2); }
		|																					{ $$ = new ListNode; }
		;

thread:
		BRANCH ':' statement_list									{	$$ = opr(BRANCH, 2, NULL, $3); }
		| BRANCH IDENTIFIER ':' statement_list		{	$$ = opr(BRANCH, 2, idcle($2,BRANCH), $4); }
		;

branch_set:
		branch_list																{ $$ = $1; }
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
		LIMIT '(' IDENTIFIER ',' limit_spec ')'	{ $$ = opr(LIMIT, 2, idcg($3,METRIC), $5); }
		;

limit_spec:
		limit_range
		| unit_qty																{ $$ = opr(RANGE, 2, new UnitsNode(0, NULL), $1); }
		;

limit_range:
		'{' unit_qty ',' unit_qty '}'							{ $$ = opr(RANGE, 2, $2, $4); }
		| '{' unit_qty '+' '}'										{ $$ = opr(RANGE, 2, $2, NULL); }
		;

count_range:
		'{' int_expr ',' int_expr '}'							{ $$ = opr(RANGE, 2, $2, $4); }
		| '{' int_expr '+' '}'										{ $$ = opr(RANGE, 2, $2, NULL); }
		;

unit_qty:
		INTEGER IDENTIFIER																{ $$ = new UnitsNode($1, $2); }
		| FLOAT IDENTIFIER																{ $$ = new UnitsNode($1, $2); }
		| AVERAGE '(' IDENTIFIER ',' IDENTIFIER ')'				{ $$ = opr(AVERAGE, 2, idcg($3,METRIC), idf($5,RECOGNIZER)); }
		| AVERAGE '(' IDENTIFIER ',' string_literal ')'		{ $$ = opr(AVERAGE, 2, idcg($3,METRIC), $5); }
		| STDDEV '(' IDENTIFIER ',' IDENTIFIER ')'				{ $$ = opr(STDDEV, 2, idcg($3,METRIC), idf($5,RECOGNIZER)); }
		| STDDEV '(' IDENTIFIER ',' string_literal ')'		{ $$ = opr(STDDEV, 2, idcg($3,METRIC), $5); }
		| F_MAX '(' IDENTIFIER ',' IDENTIFIER ')'					{ $$ = opr(F_MAX, 2, idcg($3,METRIC), idf($5,RECOGNIZER)); }
		| F_MAX '(' IDENTIFIER ',' string_literal ')'			{ $$ = opr(F_MAX, 2, idcg($3,METRIC), $5); }
		| F_MIN '(' IDENTIFIER ',' IDENTIFIER ')'					{ $$ = opr(F_MIN, 2, idcg($3,METRIC), idf($5,RECOGNIZER)); }
		| F_MIN '(' IDENTIFIER ',' string_literal ')'			{ $$ = opr(F_MIN, 2, idcg($3,METRIC), $5); }
		;

repeat_range:
		INTEGER																		{ $$ = opr(RANGE, 2, new IntNode($1), new IntNode($1)); }
		| BETWEEN INTEGER AND INTEGER							{ $$ = opr(RANGE, 2, new IntNode($2), new IntNode($4)); }
		;

path_expr:
		repeat
		| PATHVAR																	{ $$ = idf($1,PATH_VAR); }
		| PATHVAR '=' path_expr										{ $$ = opr('=', 2, idcl($1,PATH_VAR), $3); }
		;

task:
		TASK '(' string_expr ',' string_expr ')'	{ $$ = opr(TASK, 2, $3, $5); }
		;

event:
		NOTICE '(' string_expr ',' string_expr ')'	{ $$ = opr(NOTICE, 2, $3, $5); }
		;

string_literal:
		STRING																		{ $$ = new StringNode(NODE_STRING, $1); }
		| REGEX																		{ $$ = new StringNode(NODE_REGEX, $1); }
		| '*'																			{ $$ = new StringNode(NODE_WILDCARD, NULL); }
		;

string_expr:
		'!' string_expr														{ $$ = opr('!', 1, $2); }
		| string_literal
		| STRINGVAR																{ $$ = idf($1,STRING_VAR); }
		| STRINGVAR '=' string_expr								{ $$ = opr('=', 2, idcl($1,STRING_VAR), $3); }
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
		ASSERT '(' bool_expr ')' ';'							{ $$ = opr(ASSERT, 1, $3); }
		;

bool_expr:
		bool_expr B_AND bool_expr									{ $$ = opr(B_AND, 2, $1, $3); }
		| bool_expr B_OR bool_expr								{ $$ = opr(B_OR, 2, $1, $3); }
		| bool_expr IMPLIES bool_expr							{ $$ = opr(IMPLIES, 2, $1, $3); }
		| '!' bool_expr														{ $$ = opr('!', 1, $2); }
		| '(' bool_expr ')'												{ $$ = $2; }
		| int_expr IN count_range									{ $$ = opr(IN, 2, $1, $3); }
		| int_expr '<' int_expr										{ $$ = opr('<', 2, $1, $3); }
		| int_expr '>' int_expr										{ $$ = opr('>', 2, $1, $3); }
		| int_expr LE int_expr										{ $$ = opr(LE, 2, $1, $3); }
		| int_expr GE int_expr										{ $$ = opr(GE, 2, $1, $3); }
		| int_expr EQ int_expr										{ $$ = opr(EQ, 2, $1, $3); }
		| int_expr NE int_expr										{ $$ = opr(NE, 2, $1, $3); }
		| unit_qty IN limit_range									{ $$ = opr(IN, 2, $1, $3); }
		| unit_qty '<' unit_qty										{ $$ = opr('<', 2, $1, $3); }
		| unit_qty '>' unit_qty										{ $$ = opr('>', 2, $1, $3); }
		| unit_qty LE unit_qty										{ $$ = opr(LE, 2, $1, $3); }
		| unit_qty GE unit_qty										{ $$ = opr(GE, 2, $1, $3); }
		| unit_qty EQ unit_qty										{ $$ = opr(EQ, 2, $1, $3); }
		| unit_qty NE unit_qty										{ $$ = opr(NE, 2, $1, $3); }
		| float_expr '<' float_expr								{ $$ = opr('<', 2, $1, $3); }
		| float_expr '>' float_expr								{ $$ = opr('>', 2, $1, $3); }
		| float_expr LE float_expr								{ $$ = opr(LE, 2, $1, $3); }
		| float_expr GE float_expr								{ $$ = opr(GE, 2, $1, $3); }
		| float_expr EQ float_expr								{ $$ = opr(EQ, 2, $1, $3); }
		| float_expr NE float_expr								{ $$ = opr(NE, 2, $1, $3); }
		;

int_expr:
		INTEGER																		{ $$ = new IntNode($1); }
		| INSTANCES '(' IDENTIFIER ')'						{ $$ = opr(INSTANCES, 1, idf($3,RECOGNIZER)); }
		| UNIQUE '(' IDENTIFIER ')'								{ $$ = opr(UNIQUE, 1, idf($3,RECOGNIZER)); }
		| UNIQUE '(' STRINGVAR ')'								{ $$ = opr(UNIQUE, 1, idf($3,STRING_VAR)); }
		| int_expr '*' int_expr										{ $$ = opr('*', 2, $1, $3); }
		| int_expr '+' int_expr										{ $$ = opr('+', 2, $1, $3); }
		| int_expr '-' int_expr										{ $$ = opr('-', 2, $1, $3); }
		;

float_expr:
		FLOAT																			{ $$ = new FloatNode($1); }
		| int_expr '/' int_expr										{ $$ = opr('/', 2, $1, $3); }
		| unit_qty '/' unit_qty										{ $$ = opr('/', 2, $1, $3); }
		| float_expr '/' float_expr								{ $$ = opr('/', 2, $1, $3); }
		| float_expr '*' float_expr								{ $$ = opr('*', 2, $1, $3); }
		| float_expr '-' float_expr								{ $$ = opr('-', 2, $1, $3); }
		| float_expr '+' float_expr								{ $$ = opr('+', 2, $1, $3); }
		;

%%

void yyerror(const char *fmt, ...) {
	va_list args;
	fprintf(stderr, "%s:%d: ", yyfilename, yylno);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	yy_success = false;
}

bool expect_parse(const char *filename) {
	yyfilename = strdup(filename);
	yyin = fopen(filename, "r");
	if (!yyin) {
		perror(filename);
		return false;
	}
	yyparse();
	fclose(yyin);
	return yy_success;
}
