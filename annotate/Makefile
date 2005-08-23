#CPPFLAGS = -I/usr/lib/jdk1.5.0_01/include -I/usr/lib/jdk1.5.0_01/include/linux
JAVA_HOME = /home/patrick/pip/j2sdk1.4.2_04
CPPFLAGS = -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/linux
CFLAGS = -Wall -g
CXXFLAGS = $(CFLAGS)
OBJS = annotate.o socklib.o
CPPFLAGS += -DTHREADS
CC = gcc
LDFLAGS = -L.
LDLIBS = -lrt -lannotate -lpthread
#CPPFLAGS += -DPROC_GETRUSAGE
TESTS = threadtest evtest belief pushpop longid

all: libannotate.a libjannotate.so AnnTest.class $(TESTS)

annotate_Annotate.h: Annotate.class
	javah annotate.Annotate && touch $@

%.class: %.java
	javac $<

libannotate.a: $(OBJS)
	$(AR) r $@ $(OBJS)

libjannotate.so: $(OBJS) jannotate.o
	gcc -shared -o $@ $(OBJS) jannotate.o

clean:
	rm -f libannotate.a libjannotate.so jannotate.o $(OBJS) *.class annotate_Annotate.h $(TESTS)

jannotate.o:: annotate_Annotate.h
