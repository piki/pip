CFLAGS = -g -Wall `pkg-config --cflags glib-2.0`
CPPFLAGS = -I.
PROGS = devowel lineno calc expect
LDLIBS = -lm `pkg-config --libs glib-2.0`

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
	$(CC) $(CPPFLAGS) $(CFLAGS) lex.yy.c y.tab.c -o $@ -lm

expect: expect.l expect.y parsetree.c
	$(YACC) -d expect.y
	$(LEX) expect.l
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDLIBS) lex.yy.c y.tab.c parsetree.c -o $@

clean:
	rm -f $(PROGS) lex.yy.c y.tab.c y.tab.h *.o *.exe
