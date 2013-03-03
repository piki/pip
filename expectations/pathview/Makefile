CPPFLAGS = -I. -I..
CFLAGS = -g -Wall -Wno-deprecated-declarations -Werror
CXXFLAGS = -g -Wall -Wno-deprecated-declarations -I. -I.. -Werror
PROGS = pathview plottest dagtest graphtest
LDLIBS = -lm -lmysqlclient -lpcre `pkg-config --libs libglade-2.0 librsvg-2.0 gtk+-2.0`
LDFLAGS = -rdynamic
CPPFLAGS = `pkg-config --cflags libglade-2.0 librsvg-2.0 gtk+-2.0`
OBJS = pathview.o plot.o dag.o graph.o readpaths.o pathtl.o workqueue.o \
	pathstub.o \
	../path.o ../exptree.o ../expect.o ../expect.tab.o ../parsetree.o ../aggregates.o

all: $(PROGS)

pathview: $(OBJS)
	g++ $(LDFLAGS) $^ -o $@ $(LDLIBS)

plottest: plottest.o plot.o

dagtest: dagtest.o dag.o

graphtest: graphtest.o graph.o

clean:
	rm -f $(PROGS) *.o
