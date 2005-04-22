CPPFLAGS = -I/usr/lib/jdk1.5.0_01/include -I/usr/lib/jdk1.5.0_01/include/linux
CFLAGS = -Wall -g
CXXFLAGS = $(CFLAGS)
OBJS = annotate.o socklib.o
CPPFLAGS += -DTHREADS
#CPPFLAGS += -DPROC_GETRUSAGE

all: libannotate.a libjannotate.so threadtest evtest AnnTest.class belief

Annotate.h: Annotate.class
	javah Annotate && touch $@

%.class: %.java
	javac $<

threadtest: threadtest.c
	$(CC) $< -o $@ -lpthread -lrt -L. -lannotate

belief: belief.c
	$(CC) $< -o $@ -lpthread -lrt -L. -lannotate

evtest: evtest.c
	$(CC) $< -o $@ -lpthread -lrt -L. -lannotate

libannotate.a: $(OBJS)
	$(AR) r $@ $(OBJS)

libjannotate.so: $(OBJS) jannotate.o
	gcc -shared -o $@ $(OBJS) jannotate.o

clean:
	rm -f libannotate.a libjannotate.so jannotate.o $(OBJS) threadtest evtest *.class Annotate.h belief

jannotate.o:: Annotate.h
