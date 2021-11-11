CFLAGS=-std=c11 -g -static
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

pugcc: $(OBJS)
	$(CC) -o pugcc $(OBJS) $(LDFLAGS)

$(OBJS): pugcc.h

test: pugcc
	./pugcc tests > tmp.s
	echo 'int ext1; int *ext2; int char_fn() { return 257; } int static_fn() { return 5; }' | gcc -xc -c -o tmp2.o -
	gcc -static -g -o tmp tmp.s tmp2.o
	./tmp

clean:
	rm -f pugcc *.o *.~ tmp*

.PHONY: test clean
