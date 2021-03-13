CFLAGS = -g -Wall -Werror
CPPFLAGS = `/usr/bin/pkg-config --cflags libglade-2.0` -I../expectations -I../dbfill \
	-DBUILD_DIR=\"`pwd`\"
PROGS = pathview plottest dagtest
LDLIBS = -lm `/usr/bin/pkg-config --libs libglade-2.0`
LDFLAGS = -rdynamic
OBJS = pathview.o plot.o dag.o pathtl.o \
	pathstub.o \
	../expectations/path.o ../expectations/exptree.o ../expectations/expect.o \
	../expectations/expect.tab.o ../expectations/parsetree.o \
	../expectations/aggregates.o ../expectations/pathfactory.o ../dbfill/rcfile.o \
	../dbfill/pipdb.o

ifeq ("1","1")
LDLIBS += -lmysqlclient
LDFLAGS += -L/usr/lib -L/usr/lib/mysql
endif
ifeq ("1","1")
LDLIBS += -lpcre
LDFLAGS += -L/usr/lib
endif
ifeq ("1","1")
CPPFLAGS += `/usr/bin/pkg-config --cflags librsvg-2.0` -DHAVE_RSVG
LDLIBS += `/usr/bin/pkg-config --libs librsvg-2.0`
LDFLAGS += -L/usr/lib
PROGS += graphtest
OBJS += graph.o
endif
CXXFLAGS = $(CFLAGS) $(CPPFLAGS)

all: $(PROGS)

pathview: $(OBJS)
	g++ $(LDFLAGS) $^ -o $@ $(LDLIBS)

plottest: plottest.o plot.o

dagtest: dagtest.o dag.o

graphtest: graphtest.o graph.o

clean:
	rm -f $(PROGS) *.o *.bak gmon.out
