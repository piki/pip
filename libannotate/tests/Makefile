CFLAGS = -Wall -g
CXXFLAGS = $(CFLAGS)
CPPFLAGS = -I..
CC = gcc
LDFLAGS = -L..
LDLIBS = -lannotate -lpthread #-lz
TESTS = belief evtest longid pushpop busy threadtest

all: $(TESTS)

clean:
	rm -f $(TESTS)
