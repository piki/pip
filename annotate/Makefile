CPPFLAGS = -I/usr/lib/jdk1.5.0_01/include -I/usr/lib/jdk1.5.0_01/include/linux
CFLAGS = -Wall -g -Werror
CXXFLAGS = $(CFLAGS)
OBJS = annotate.o
CPPFLAGS += -DTHREADS

all: libannotate.a libjannotate.so threadtest

Annotate.h: Annotate.class
	javah Annotate

Annotate.class: Annotate.java
	javac $<

threadtest: threadtest.c
	$(CC) $< -o $@ -lpthread -lrt -L. -lannotate

libannotate.a: $(OBJS)
	$(AR) r $@ $(OBJS)

libjannotate.so: $(OBJS) jannotate.o
	gcc -shared -o $@ $(OBJS) jannotate.o

clean:
	rm -f libannotate.a libjannotate.so jannotate.o $(OBJS) threadtest *.class Annotate.h

jannotate.o:: Annotate.h
