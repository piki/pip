CXXFLAGS = -Wall -Werror -g
CC = g++
LDLIBS = -lmysqlclient

all: annotrans dbfill

annotrans: events.o annotrans.o

dbfill: events.o dbfill.o

clean:
	rm -f *.o annotrans dbfill
