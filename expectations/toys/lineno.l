 int yylineno;
%%
^.*\n  printf("%4d\t%s", ++yylineno, yytext);
%%
