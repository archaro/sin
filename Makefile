CC=gcc
CFLAGS=-g
LDFLAGS=
LIBS=

%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

all: sin scomp

clean:
	rm -f *.o sin scomp parser.c parser.h lexer.c

.PHONY: all clean

sin: main.o stack.o interpret.o item.o log.o value.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

scomp: parser.o lexer.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

interpret.o: interpret.c interpret.h item.h value.h stack.h
item.o: item.c item.h value.h stack.h
log.o: log.c log.h
main.o: main.c log.h value.h item.h stack.h interpret.h
value.o: value.c value.h
stack.o: stack.c stack.h value.h

parser.c: parser.y
	$(YACC) -o parser.c --defines=parser.h parser.y
  
lexer.c: lexer.l parser.h
	$(LEX) -o lexer.c lexer.l

