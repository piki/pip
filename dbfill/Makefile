CXXFLAGS = -Wall -Werror -g -O3
CC = g++
PROGS = annotrans beliefcheck new-reconcile
ifeq ("1","1")
LDFLAGS += -L/usr/lib -L/usr/lib/mysql
LDLIBS += -lmysqlclient
PROGS += dbfill
endif
ifeq ("","1")
LDLIBS += -L/usr/lib -ludns
PROGS += loglistener
endif
OBJS = client.o events.o insertbuffer.o reconcile.o rcfile.o

all: $(PROGS)

annotrans: events.o annotrans.o

new-reconcile: events.o pipdb.o new-reconcile.o
	$(CC) $^ -o $@

beliefcheck: events.o beliefcheck.o

dbfill: events.o dbfill.o $(OBJS)

loglistener: events.o loglistener.o $(OBJS)

clean:
	rm -f *.o annotrans beliefcheck dbfill loglistener new-reconcile
