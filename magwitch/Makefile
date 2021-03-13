OPT = 
#PROFILE = -pg
OPTIMIZE = -O3
CC = g++
CFLAGS = $(OPTIMIZE) -g -Wall $(OPT) $(PROFILE)
CXXFLAGS = $(OPTIMIZE) -g -Wall $(OPT) $(PROFILE)
CPPFLAGS = -I. -I../dbfill
PROGS = parsetest
LDLIBS = -lmysqlclient -lpcre
LDFLAGS = $(PROFILE) -L/usr/lib -L/usr/lib -L/usr/lib/mysql
YACC = /usr/bin/bison
LEX = flex

.PRECIOUS: expect.c

all: $(PROGS)


parsetest: expect.tab.o parsetree.o exptree.o path.o parsetest.o expect.o

expect.tab.cc: expect.yy
	$(YACC) -d expect.yy

clean:
	rm -f $(PROGS) expect.c expect.tab.* *.o gmon.out

expect.tab.hh:: expect.tab.cc
parsetree.o:: parsetree.cc expect.tab.hh
exptree.o:: exptree.cc expect.tab.hh
expect.o:: expect.tab.o
parsetest.o:: expect.tab.o
