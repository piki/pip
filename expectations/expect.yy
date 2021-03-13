/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

// vim: set sw=2 ts=2 tw=0:
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
%token INVALIDATOR
%token RECOGNIZER
%token FRAGMENT
%token LEVEL
%token REPEAT
%token BRANCH
%token XOR
%token MAYBE
%token CALL
%token FUTURE
%token DONE
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
%token F_POW
%token SQRT
%token LOG
%token LOGN
%token EXP
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
%token SEND
%token RECV
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
%token <sValue> VARIABLE
%type <iValue> VALIDATOR INVALIDATOR RECOGNIZER pathtype
%type <nList> limit_list statement_list thread_list xor_list path_limits msg_list
%type <nPtr> statement thread thread_count_range pathboolexpr msg_target
%type <nPtr> limit level limit_range limit_spec repeat_range
%type <nPtr> string_expr simple_string_expr int_expr simple_int_expr xor task assert assertdecl bool_expr
%type <nPtr> float_expr window string_literal unit_qty count_range int_func string_func

%left B_AND B_OR IMPLIES
%left '<' '>' LE GE EQ NE
%left '+' '-'
%left '*' '/'
%right F_POW
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
		pathtype IDENTIFIER '{' path_limits thread_list '}' {
			if (this_path_ok) add_recognizer(new PathRecognizer(idcge($2,RECOGNIZER), dynamic_cast<ListNode*>($4), dynamic_cast<ListNode*>($5), true, $1));
			delete $4;
			this_path_ok = true;
		}
		| FRAGMENT pathtype IDENTIFIER '{' path_limits statement_list '}' {
			if (this_path_ok) add_recognizer(new PathRecognizer(idcge($3,RECOGNIZER), dynamic_cast<ListNode*>($5), dynamic_cast<ListNode*>($6), false, $2));
			delete $5;
			this_path_ok = true;
		}
		| pathtype IDENTIFIER ':' pathboolexpr ';' {
			if (this_path_ok) add_recognizer(new SetRecognizer(idcge($2,RECOGNIZER), $4, $1));
			this_path_ok = true;
		}
		;

pathtype:
		VALIDATOR							{ $$ = VALIDATOR; }
		| INVALIDATOR					{ $$ = INVALIDATOR; }
		| RECOGNIZER					{ $$ = RECOGNIZER; }
		;

pathboolexpr:
		pathboolexpr B_AND pathboolexpr						{ $$ = opr(B_AND, 2, $1, $3); }
		| pathboolexpr B_OR pathboolexpr					{ $$ = opr(B_OR, 2, $1, $3); }
		| pathboolexpr IMPLIES pathboolexpr				{ $$ = opr(IMPLIES, 2, $1, $3); }
		| '!' pathboolexpr												{ $$ = opr('!', 1, $2); }
		| '(' pathboolexpr ')'										{ $$ = $2; }
		| IDENTIFIER															{ $$ = idf($1,RECOGNIZER); }
		;

statement_list:
		statement_list statement									{ $$ = $1; ($$)->add($2); }
		|																					{ $$ = new ListNode; }
		;
		
/* The placement of semicolons is a little hackish here.  We want to be
 * able to say "$n = task(*) {...}" or "$n = task(*);" or "$n=$z*2;".  I
 * get this by forcing statement, int_expr, string_expr, int_func, and
 * string_func to be self-terminating (i.e., end in ';' or '}'). */
statement:
		string_func
		| int_func
		| CALL '(' IDENTIFIER ')' ';'							{ $$ = opr(CALL, 1, idf($3,RECOGNIZER)); }   //?
		| XOR '{' xor_list '}'										{ $$ = opr(XOR, 1, $3); }
		| MAYBE statement													{ $$ = opr(REPEAT, 2, opr(RANGE, 2, new IntNode(0), new IntNode(1)), $2); }
		| FUTURE statement												{ $$ = opr(FUTURE, 2, NULL, $2); }
		| FUTURE IDENTIFIER statement							{ $$ = opr(FUTURE, 2, idcl($2,FUTURE), $3); }
		| DONE '(' IDENTIFIER ')' ';'							{ $$ = opr(DONE, 1, idf($3,FUTURE)); }
		| ANY	'(' ')' ';'													{ $$ = opr(ANY, 0); }
		| '{' statement_list '}'									{ $$ = $2; }
		| VARIABLE '=' int_expr										{ $$ = opr('=', 2, idcl($1,INT_VAR), $3); }
		| VARIABLE '=' string_expr									{ $$ = opr('=', 2, idcl($1,STRING_VAR), $3); }
		| error ';'																{ $$ = NULL; }
		;

/* Why string_expr vs. simple_string_expr and int_expr vs.
 * simple_int_expr?  In addition to the above?  Because we don't really
 * want to allow "repeat" to be used as an argument to "repeat," or "send"
 * as an argument to "send," etc. */
string_expr:
		string_func
		| simple_string_expr ';'
		;

string_func:
		SEND '(' msg_target ')' limit_list ';'		{ $$ = opr(SEND, 2, $3, $5); }
		| RECV '(' msg_target ')' limit_list ';'	{ $$ = opr(RECV, 2, $3, $5); }
		| NOTICE '(' simple_string_expr ')' ';'		{ $$ = opr(NOTICE, 1, $3); }
		| task limit_list ';'											{ $$ = opr(TASK, 3, $1, $2, NULL); }
		| task limit_list '{' statement_list '}'	{ $$ = opr(TASK, 3, $1, $2, $4); }
		;

simple_string_expr:
		string_literal
		| VARIABLE																{ $$ = idf($1,STRING_VAR); }
		| '!' simple_string_expr									{ $$ = opr('!', 1, $2); }
		;

int_expr:
		int_func
		| simple_int_expr ';'
		;

int_func:
		REPEAT repeat_range statement							{ $$ = opr(REPEAT, 2, $2, $3); }
		;

simple_int_expr:
		INTEGER																		{ $$ = new IntNode($1); }
		| INSTANCES '(' IDENTIFIER ')'						{ $$ = opr(INSTANCES, 1, idf($3,RECOGNIZER)); }
		| UNIQUE '(' IDENTIFIER ')'								{ $$ = opr(UNIQUE, 1, idf($3,RECOGNIZER)); }
		| UNIQUE '(' VARIABLE ')'									{ $$ = opr(UNIQUE, 1, idf($3,STRING_VAR)); }
		| simple_int_expr '*' simple_int_expr			{ $$ = opr('*', 2, $1, $3); }
		| simple_int_expr '+' simple_int_expr			{ $$ = opr('+', 2, $1, $3); }
		| simple_int_expr '-' simple_int_expr			{ $$ = opr('-', 2, $1, $3); }
		| VARIABLE																{ $$ = idf($1,INT_VAR); }
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

msg_target:
		msg_list																	{ $$ = $1; }
		| simple_string_expr
		;

msg_list:
		msg_list ',' IDENTIFIER										{ $$ = $1; ($$)->add(idcl($3,THREAD)); }
		| IDENTIFIER															{ $$ = new ListNode; ($$)->add(idcl($1,THREAD)); }
		;

thread:
		THREAD IDENTIFIER '(' simple_string_expr ',' thread_count_range ')' '{' path_limits statement_list '}' {
			$$ = opr(THREAD, 5, idcl($2,THREAD), $4, $6, $9, $10);
		}
		;

thread_count_range:
		INTEGER																		{ $$ = opr(RANGE, 2, new IntNode($1), new IntNode($1)); }
		| '{' INTEGER ',' INTEGER '}'							{ $$ = opr(RANGE, 2, new IntNode($2), new IntNode($4)); }
		;

path_limits:
		path_limits limit ';'											{ $$ = $1; ($$)->add($2); }
		| path_limits level ';'										{ $$ = $1; ($$)->add($2); }
		|																					{ $$ = new ListNode; }
		;

limit_list:
		limit_list limit													{ $$ = $1; ($$)->add($2); }
		|																					{ $$ = new ListNode; }
		;

limit:
		LIMIT '(' IDENTIFIER ',' limit_spec ')'		{ $$ = opr(LIMIT, 2, idcg($3,METRIC), $5); }
		;

level:
		LEVEL '(' INTEGER ')'											{ $$ = opr(LEVEL, 1, new IntNode($3)); }

limit_spec:
		limit_range
		| count_range
		| INTEGER																	{ $$ = opr(RANGE, 2, NULL, new IntNode($1)); }
		| unit_qty																{ $$ = opr(RANGE, 2, NULL, $1); }
		;

limit_range:
		'{' unit_qty ',' unit_qty '}'							{ $$ = opr(RANGE, 2, $2, $4); }
		| '{' unit_qty '+' '}'										{ $$ = opr(RANGE, 2, $2, NULL); }
		| '{' '=' unit_qty '}'										{ $$ = opr(RANGE, 1, $3); }
		;

count_range:
		'{' simple_int_expr ',' simple_int_expr '}'			{ $$ = opr(RANGE, 2, $2, $4); }
		| '{' simple_int_expr '+' '}'										{ $$ = opr(RANGE, 2, $2, NULL); }
		| '{' '=' simple_int_expr '}'										{ $$ = opr(RANGE, 1, $3); }
		| '{' float_expr ',' float_expr '}'							{ $$ = opr(RANGE, 2, $2, $4); }
		| '{' float_expr '+' '}'												{ $$ = opr(RANGE, 2, $2, NULL); }
		| '{' '=' float_expr '}'												{ $$ = opr(RANGE, 1, $3); }
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
		simple_int_expr																	{ $$ = opr(RANGE, 2, $1, $1); }
		| BETWEEN simple_int_expr AND simple_int_expr		{ $$ = opr(RANGE, 2, $2, $4); }
		;

task:
		TASK '(' simple_string_expr ')'						{ $$ = opr(TASK, 1, $3); }
		;

string_literal:
		STRING																		{ $$ = new StringNode(NODE_STRING, $1); }
		| REGEX																		{ $$ = new StringNode(NODE_REGEX, $1); }
		| '*'																			{ $$ = new StringNode(NODE_WILDCARD, NULL); }
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
		| simple_int_expr IN count_range					{ $$ = opr(IN, 2, $1, $3); }
		| simple_int_expr '<' simple_int_expr			{ $$ = opr('<', 2, $1, $3); }
		| simple_int_expr '>' simple_int_expr			{ $$ = opr('>', 2, $1, $3); }
		| simple_int_expr LE simple_int_expr			{ $$ = opr(LE, 2, $1, $3); }
		| simple_int_expr GE simple_int_expr			{ $$ = opr(GE, 2, $1, $3); }
		| simple_int_expr EQ simple_int_expr			{ $$ = opr(EQ, 2, $1, $3); }
		| simple_int_expr NE simple_int_expr			{ $$ = opr(NE, 2, $1, $3); }
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

float_expr:
		FLOAT																			{ $$ = new FloatNode($1); }
		| simple_int_expr '/' simple_int_expr			{ $$ = opr('/', 2, $1, $3); }
		| unit_qty '/' unit_qty										{ $$ = opr('/', 2, $1, $3); }
		| float_expr '/' float_expr								{ $$ = opr('/', 2, $1, $3); }
		| float_expr '*' float_expr								{ $$ = opr('*', 2, $1, $3); }
		| float_expr '-' float_expr								{ $$ = opr('-', 2, $1, $3); }
		| float_expr '+' float_expr								{ $$ = opr('+', 2, $1, $3); }
		| float_expr F_POW float_expr							{ $$ = opr(F_POW, 2, $1, $3); }
		| LOG '(' float_expr ')'									{ $$ = opr(LOG, 1, $3); }
		| LOGN '(' float_expr ')'									{ $$ = opr(LOGN, 1, $3); }
		| EXP '(' float_expr ')'									{ $$ = opr(EXP, 1, $3); }
		| SQRT '(' float_expr ')'									{ $$ = opr(SQRT, 1, $3); }
		;

%%

void yyerror(const char *fmt, ...) {
	va_list args;
	fprintf(stderr, "%s:%d: ", yyfilename, yylno);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	this_path_ok = yy_success = false;
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
