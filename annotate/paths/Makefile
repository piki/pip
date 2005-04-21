CXXFLAGS = -Wall -Werror -g
CPPFLAGS = -I../../../../c/libpiki
CC = g++
LDFLAGS = -L../../../../c/libpiki
LDLIBS = -lmysqlclient -lpiki

all: annotrans beliefcheck dbfill loglistener

annotrans: events.o annotrans.o

beliefcheck: events.o beliefcheck.o

dbfill: events.o dbfill.o client.o reconcile.o

loglistener: events.o loglistener.o client.o reconcile.o

clean:
	rm -f *.o annotrans beliefcheck dbfill loglistener
