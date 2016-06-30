# Makefile for intercept
#
CFLAGS = -std=gnu99 -Wall -Werror -fPIC -O0 -ggdb -DDEBUG

all:  intercept.so tester

tester: tester.o
	$(CC) -o tester tester.o

intercept.so: intercept.o
	$(CC) -Wl,-z,relro -shared -Wl,-soname,intercept.so -o intercept.so intercept.o

test: all
	LD_PRELOAD=intercept.so LD_LIBRARY_PATH=.:LD_LIBRARY_PATH ./tester

clobber: clean
	rm -f intercept.so tester

clean:
	rm -f *.o core a.out
