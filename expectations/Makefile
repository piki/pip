CFLAGS = -g -Wall
CPPFLAGS = -I.
PROGS = expect
LDLIBS = -lm
YACC = bison
LEX = flex

all: $(PROGS)

expect: expect.l expect.yy parsetree.cc
	$(YACC) -d expect.yy
	$(LEX) expect.l
	g++ $(CPPFLAGS) $(CFLAGS) $(LDLIBS) lex.yy.c expect.tab.cc parsetree.cc -o $@

clean:
	rm -f $(PROGS) lex.yy.c lex.yy.cc y.tab.c y.tab.h *.o *.exe \
		*.tab.cc *.tab.hh *.tab.c *.tab.h
