CC=gcc
CFLAGS=-g -Wall -MMD -MP
LDFLAGS=
LIBS=
YACC=bison
LEX=flex
DEBUG=-DDEBUG=1

SRC_DIR := src
OBJ_DIR := obj

# Collect source files (excluding scomp.c) and generate object file paths
SOURCES := $(filter-out $(SRC_DIR)/scomp.c, $(wildcard $(SRC_DIR)/*.c))
OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(DEBUG) $< -o $@

.PHONY: all clean

all: sin scomp

clean:
	rm -rf $(OBJ_DIR)/*.o $(OBJ_DIR)/*.d sin scomp $(SRC_DIR)/parser.c \
         $(SRC_DIR)/parser.h $(SRC_DIR)/lexer.c

sin: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

scomp: $(OBJ_DIR)/parser.o $(OBJ_DIR)/lexer.o $(OBJ_DIR)/memory.o $(OBJ_DIR)/log.o $(OBJ_DIR)/scomp.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)
 
$(SRC_DIR)/parser.c $(SRC_DIR)/parser.h: $(SRC_DIR)/parser.y
	$(YACC) -o $(SRC_DIR)/parser.c --defines=$(SRC_DIR)/parser.h \
          $(SRC_DIR)/parser.y
  
$(SRC_DIR)/lexer.c: $(SRC_DIR)/lexer.l $(SRC_DIR)/parser.h
	$(LEX) -o $(SRC_DIR)/lexer.c $(SRC_DIR)/lexer.l

-include $(DEPS)
