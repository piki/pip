CC = g++
CFLAGS = -g -Wall
CXXFLAGS = -g -Wall
CPPFLAGS = -I.
PROGS = pathcheck
LDLIBS = -lm -lmysqlclient
YACC = bison

.PRECIOUS: expect.c

all: $(PROGS) exptree.o

pathcheck: expect.tab.o parsetree.o exptree.o path.o pathcheck.o expect.o

expect.tab.cc: expect.yy
	$(YACC) -d expect.yy

clean:
	rm -f $(PROGS) expect.c expect.tab.* *.o

parsetree.o:: parsetree.cc expect.tab.hh
exptree.o:: exptree.cc expect.tab.hh
expect.o:: expect.tab.o
