CPPFLAGS = -I/usr/lib/jdk1.5.0_01/include -I/usr/lib/jdk1.5.0_01/include/linux -DTHREADS
CFLAGS = -Wall -g -Werror
CXXFLAGS = $(CFLAGS)
OBJS = annotate.o

all: libannotate.a libjannotate.so annotrans threadtest

threadtest: threadtest.c
	$(CC) $< -o $@ -lpthread -lrt -L. -lannotate

libannotate.a: $(OBJS)
	$(AR) r $@ $(OBJS)

libjannotate.so: $(OBJS) jannotate.o
	gcc -shared -o $@ $(OBJS) jannotate.o

clean:
	rm -f libannotate.a libjannotate.so jannotate.o $(OBJS) annotrans threadtest
