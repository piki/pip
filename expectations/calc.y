%union {
	int iValue;
	double fValue;
};

%{
#include <math.h>
#include <stdio.h>
	int yylex(void);
	void yyerror(char*);
	double sym[26];
	double stored;
%}
%token <iValue> VARIABLE
%token <fValue> FLOAT
%token SIN
%token COS
%token TAN
%token SQRT
%token PI
%left '+' '-'
%left '*' '/'
%right '^'
%nonassoc UMINUS
%type <fValue> statement expr

%%

program:
		program statement '\n'		{ printf("> "); fflush(stdout); }
		| program '\n'						{ printf("> "); fflush(stdout); }
		|
		;

statement:
		expr											{	printf("%f\n", $1); stored = $1; }
		| VARIABLE '=' expr				{ sym[$1] = $3; }
		;

expr:
		FLOAT
		| VARIABLE								{ $$ = sym[$1]; }
		| '_'											{ $$ = stored; }
		| '-' expr %prec UMINUS		{ $$ = -$2; }
		| expr '+' expr						{ $$ = $1 + $3; }
		| expr '-' expr						{ $$ = $1 - $3; }
		| expr '*' expr						{ $$ = $1 * $3; }
		| expr '/' expr						{ $$ = $1 / $3; }
		| expr '^' expr						{ $$ = pow($1, $3); }
		| SIN '(' expr ')'				{ $$ = sin($3); }
		| COS '(' expr ')'				{ $$ = cos($3); }
		| TAN '(' expr ')'				{ $$ = tan($3); }
		| SQRT '(' expr ')'				{ $$ = sqrt($3); }
		| PI											{ $$ = M_PI; }
		| '(' expr ')'						{ $$ = $2; }
		;

%%

void yyerror(char *s) {
	fprintf(stderr, "%s\n", s);
}

int main(void) {
	printf("> "); fflush(stdout);
	yyparse();
	return 0;
}
