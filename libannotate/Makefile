JAVA_HOME = 
CPPFLAGS = -I$(JAVA_HOME)/include #-I$(JAVA_HOME)/include/linux
CFLAGS = -Wall -Werror -g -O3
CXXFLAGS = $(CFLAGS)
OBJS = annotate.o socklib.o
CPPFLAGS += -DTHREADS
CC = gcc
LDFLAGS = -L.
LDLIBS = -lrt -lannotate -lpthread
SUBDIRS =  tests

all: libannotate.a  #dicttest
	set -e ; for i in $(SUBDIRS); do $(MAKE) -C $$i all; done

annotate_Annotate.h: annotate/Annotate.class
	javah -o $@ annotate/Annotate.class || rm $@

%.class: %.java
	javac $<

libannotate.a: $(OBJS)
	$(AR) r $@ $(OBJS)

libjannotate.so: $(OBJS) jannotate.o
	gcc -shared -o $@ $(OBJS) jannotate.o

dicttest: dicttest.o dict.o

clean:
	rm -f libannotate.a libjannotate.so jannotate.o $(OBJS) *.class annotate_Annotate.h
	set -e ; for i in $(SUBDIRS); do $(MAKE) -C $$i clean; done

jannotate.o:: annotate_Annotate.h
