CFLAGS=-std=c11 -g -static
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

pugcc: $(OBJS)
	$(CC) -o pugcc $(OBJS) $(LDFLAGS)

$(OBJS): pugcc.h

test: pugcc
	./pugcc tests > tmp.s
	gcc -static -g -o tmp tmp.s
	./tmp

clean:
	rm -f pugcc *.o *.~ tmp*

.PHONY: test clean
