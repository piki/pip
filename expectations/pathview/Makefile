CPPFLAGS = -I. -I..
CFLAGS = -g -Wall
CXXFLAGS = -g -Wall -I. -I..
PROGS = pathview plottest dagtest graphtest
LDLIBS = -lm -lmysqlclient `pkg-config --libs libglade-2.0`
CPPFLAGS = `pkg-config --cflags libglade-2.0`
OBJS = pathview.o plot.o dag.o graph.o readpaths.o pathtl.o workqueue.o \
	../path.o
YACC = bison

all: $(PROGS)

pathview: $(OBJS)
	g++ $^ -o $@ $(LDLIBS)

plottest: plottest.o plot.o

dagtest: dagtest.o dag.o

graphtest: graphtest.o graph.o

clean:
	rm -f $(PROGS) *.o
