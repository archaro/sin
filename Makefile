CC=g++
CFLAGS=-g -fPIC
LDFLAGS=-lboost_serialization -lboost_iostreams
LIBS=
YACC=bison
DEBUG=-DDEBUG=1

%.o : %.c
	$(CC) -c $(CFLAGS) $(DEBUG) $< -o $@

%.o : %.cpp
	$(CC) -c $(CFLAGS) $(DEBUG) $< -o $@

all: sin scomp

clean:
	rm -f *.o sin scomp parser.c parser.h lexer.c

.PHONY: all clean

sin: value.o main.o stack.o interpret.o item.o log.o memory.o \
 Intern.o xxhash.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

scomp: parser.o lexer.o memory.o log.o scomp.o Intern.o xxhash.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

interpret.o: interpret.c interpret.h item.h value.h stack.h log.h \
 memory.h
item.o: item.c item.h value.h stack.h memory.h
lexer.o: lexer.c parser.h
log.o: log.c log.h memory.h
main.o: main.c memory.h log.h value.h item.h stack.h interpret.h
memory.o: memory.c memory.h log.h
parser.o: parser.c parser.h memory.h log.h
scomp.o: scomp.c parser.h memory.h log.h
stack.o: stack.c stack.h value.h memory.h
value.o: value.c value.h
xxhash.o: xxhash.c xxhash.h
Intern.o: Intern.cpp Intern.h tsl/robin_map.h tsl/robin_hash.h \
 tsl/robin_growth_policy.h xxhash.h

parser.c: parser.y
	$(YACC) -o parser.c --defines=parser.h parser.y
  
lexer.c: lexer.l parser.h
	$(LEX) -o lexer.c lexer.l

