OPT = 
PROFILE = -pg
CC = g++
CFLAGS = -g -Wall $(OPT) $(PROFILE)
CXXFLAGS = -g -Wall $(OPT) $(PROFILE)
CPPFLAGS = -I.
PROGS = pathcheck uniqpaths showpaths makeexp
LDLIBS = -lm -lmysqlclient -lpcre
LDFLAGS = $(PROFILE)
YACC = bison

.PRECIOUS: expect.c

all: $(PROGS)

pathcheck: expect.tab.o parsetree.o exptree.o path.o pathcheck.o expect.o aggregates.o

uniqpaths: path.o uniqpaths.o

showpaths: path.o showpaths.o

makeexp: path.o makeexp.o

expect.tab.cc: expect.yy
	$(YACC) -d expect.yy

clean:
	rm -f $(PROGS) expect.c expect.tab.* *.o

parsetree.o:: parsetree.cc expect.tab.hh
exptree.o:: exptree.cc expect.tab.hh
expect.o:: expect.tab.o
pathcheck.o:: expect.tab.o
