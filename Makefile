CFLAGS=-std=c11 -g -static

pugcc: pugcc.c

test: pugcc
	./test.sh

clean:
	rm -f pugcc *.o *.~ tmp*

.PHONY: test clean
