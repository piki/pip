CFLAGS = -g -Wall
CPPFLAGS = -I.
PROGS = devowel lineno calc expect
LDLIBS = -lm
YACC = bison
LEX = flex

all: $(PROGS)

devowel: devowel.l
	$(LEX) $<
	$(CC) $(CPPFLAGS) $(CFLAGS) lex.yy.c -o $@ -lfl

lineno: lineno.l
	$(LEX) $<
	$(CC) $(CPPFLAGS) $(CFLAGS) lex.yy.c -o $@ -lfl

calc: calc.l calc.y
	$(YACC) -d calc.y
	$(LEX) calc.l
	$(CC) $(CPPFLAGS) $(CFLAGS) lex.yy.c calc.tab.c -o $@ -lm

expect: expect.l expect.yy parsetree.cc
	$(YACC) -d expect.yy
	$(LEX) expect.l
	g++ $(CPPFLAGS) $(CFLAGS) $(LDLIBS) lex.yy.c expect.tab.cc parsetree.cc -o $@

clean:
	rm -f $(PROGS) lex.yy.c lex.yy.cc y.tab.c y.tab.h *.o *.exe \
		*.tab.cc *.tab.hh *.tab.c *.tab.h
