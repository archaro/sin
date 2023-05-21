CC=gcc
CFLAGS=-g
LDFLAGS=
LIBS=
YACC=bison

%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

all: sin scomp

clean:
	rm -f *.o sin scomp parser.c parser.h lexer.c

.PHONY: all clean

sin: main.o stack.o interpret.o item.o log.o value.o memory.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

scomp: parser.o lexer.o memory.o log.o scomp.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

interpret.o: interpret.c interpret.h item.h value.h stack.h
item.o: item.c item.h value.h stack.h
log.o: log.c log.h
main.o: main.c log.h value.h item.h stack.h interpret.h
memory.o: memory.c memory.h
value.o: value.c value.h
stack.o: stack.c stack.h value.h
scomp.o: scomp.c parser.h memory.h log.h

parser.c: parser.y
	$(YACC) -o parser.c --defines=parser.h parser.y
  
lexer.c: lexer.l parser.h
	$(LEX) -o lexer.c lexer.l

