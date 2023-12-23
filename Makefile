CC = gcc
CFLAGS = -g -Wall -MMD -MP
LDFLAGS =
LIBS =
YACC = bison
LEX = flex
DEBUG = -DDEBUG=1

SRC_DIR := src
OBJ_DIR := obj
LIB_DIR := lib

# Dependency files
DEPS := $(OBJECTS:.o=.d)

# Library of shared functions
LIB := $(LIB_DIR)/libsinshared.a
LIB_OBJECTS := $(OBJ_DIR)/log.o $(OBJ_DIR)/memory.o $(OBJ_DIR)/slab.o \
               $(OBJ_DIR)/itoa.o

# Source files for scomp
SCOMP_SOURCES := $(SRC_DIR)/scomp.c
SCOMP_OBJECTS := $(SCOMP_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Parser files for scomp
PARSER_SOURCES := $(SRC_DIR)/parser.y
PARSER_GENERATED := $(SRC_DIR)/parser.c $(SRC_DIR)/parser.h

# Lexer files for scomp
LEXER_SOURCES := $(SRC_DIR)/lexer.l
LEXER_GENERATED := $(SRC_DIR)/lexer.c

# Source files for sdiss
SDISS_SOURCES := $(SRC_DIR)/sdiss.c
SDISS_OBJECTS := $(SDISS_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Source files for sin
SIN_SOURCES := $(SRC_DIR)/sin.c $(SRC_DIR)/interpret.c \
               $(SRC_DIR)/item.c $(SRC_DIR)/stack.c $(SRC_DIR)/value.c
SIN_OBJECTS := $(SIN_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(DEBUG) $< -o $@

.PHONY: all clean lib

all: $(LIB) scomp sdiss sin

lib: $(LIB)

$(LIB): $(LIB_OBJECTS)
	@mkdir -p $(LIB_DIR)
	ar rcs $@ $^

scomp: $(PARSER_GENERATED) $(LEXER_GENERATED) $(SCOMP_OBJECTS) $(LIB)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

sdiss: $(SDISS_OBJECTS) $(LIB)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

sin: $(SIN_OBJECTS) $(LIB)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

$(PARSER_GENERATED): $(PARSER_SOURCES)
	$(YACC) -o $(SRC_DIR)/parser.c --defines=$(SRC_DIR)/parser.h $<

$(LEXER_GENERATED): $(LEXER_SOURCES) $(PARSER_GENERATED)
	$(LEX) -o $(SRC_DIR)/lexer.c $<

# Include dependency files
-include $(DEPS)

clean:
	rm -rf $(OBJ_DIR)/*.o $(OBJ_DIR)/*.d $(LIB) $(LIB_DIR)

