CC=gcc
CFLAGS=-g
LDFLAGS=
LIBS=

%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

all: sin maketest

clean:
	rm -f *.o maketest.c sin maketest

.PHONY: all clean

sin: main.o stack.o interpret.o item.o log.o value.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

maketest: maketest.c

interpret.o: interpret.c interpret.h item.h value.h stack.h
item.o: item.c item.h value.h stack.h
log.o: log.c log.h
main.o: main.c log.h value.h item.h stack.h interpret.h
value.o: value.c value.h
stack.o: stack.c stack.h value.h

maketest.c: maketest.l
