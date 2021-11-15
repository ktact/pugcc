CFLAGS=-std=c11 -g -static
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

pugcc: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): pugcc.h

pugcc-gen2: pugcc $(SRCS) pugcc.h
	./self.sh

extern.o: extern-tests
	gcc -xc -c -o extern.o extern-tests

test: pugcc extern.o
	./pugcc tests > tmp.s
	gcc -static -g -o tmp tmp.s extern.o
	./tmp

test-gen2: pugcc-gen2 extern.o
	./pugcc-gen2 tests > tmp.s
	gcc -static -g -o tmp tmp.s extern.o
	./tmp

clean:
	rm -fr pugcc pugcc-gen* *.o *.~ tmp*

.PHONY: test clean
