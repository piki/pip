/*
 * Copyright (c) 2005-2006 Duke University.  All rights reserved.
 * Please see COPYING for license terms.
 */

 int yylineno;
%%
^.*\n  printf("%4d\t%s", ++yylineno, yytext);
%%
