CC = g++
CFLAGS = -g -Wall -Werror -O3   # -pg
CXXFLAGS = $(CFLAGS)
CPPFLAGS = -I. -I../dbfill
PROGS = pathcheck uniqpaths showpaths makeexp parsetest
LDLIBS = -lm
LDFLAGS = $(PROFILE)
ifeq ("1","1")
LDLIBS += -lmysqlclient
LDFLAGS += -L/usr/lib -L/usr/lib/mysql
CPPFLAGS += -DHAVE_MYSQL
endif
ifeq ("1","1")
LDLIBS += -lpcre
LDFLAGS += -L/usr/lib
endif
YACC = /usr/bin/bison
LEX = flex

.PRECIOUS: expect.c

all: $(PROGS)

pathcheck: expect.tab.o parsetree.o exptree.o path.o pathfactory.o pathcheck.o expect.o aggregates.o ../dbfill/rcfile.o ../dbfill/pipdb.o

parsetest: expect.tab.o parsetree.o exptree.o path.o parsetest.o expect.o aggregates.o

uniqpaths: path.o pathfactory.o uniqpaths.o ../dbfill/rcfile.o ../dbfill/pipdb.o

showpaths: path.o pathfactory.o showpaths.o ../dbfill/rcfile.o ../dbfill/pipdb.o

makeexp: path.o pathfactory.o makeexp.o ../dbfill/rcfile.o ../dbfill/pipdb.o

expect.tab.cc: expect.yy
	$(YACC) -d expect.yy

clean:
	rm -f $(PROGS) expect.c expect.tab.* *.o gmon.out

expect.tab.hh:: expect.tab.cc
parsetree.o:: parsetree.cc expect.tab.hh
exptree.o:: exptree.cc expect.tab.hh
aggregates.o:: aggregates.cc expect.tab.hh
expect.o:: expect.tab.o
pathcheck.o:: expect.tab.o
parsetest.o:: expect.tab.o
