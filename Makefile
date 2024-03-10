CC = gcc
CFLAGS = -g -Wall -MMD -MP
LDFLAGS = -g
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
               $(OBJ_DIR)/itoa.o $(OBJ_DIR)/parser.o $(OBJ_DIR)/lexer.o

# Parser files for library
PARSER_SOURCES := $(SRC_DIR)/parser.y
PARSER_GENERATED := $(SRC_DIR)/parser.c $(SRC_DIR)/parser.h

# Lexer files for library
LEXER_SOURCES := $(SRC_DIR)/lexer.l
LEXER_GENERATED := $(SRC_DIR)/lexer.c

# Source files for scomp
SCOMP_SOURCES := $(SRC_DIR)/scomp.c
SCOMP_OBJECTS := $(SCOMP_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Source files for sdiss
SDISS_SOURCES := $(SRC_DIR)/sdiss.c
SDISS_OBJECTS := $(SDISS_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Source files for sin
SIN_SOURCES := $(SRC_DIR)/sin.c $(SRC_DIR)/vm.c $(SRC_DIR)/interpret.c \
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

scomp: $(SCOMP_OBJECTS) $(LIB)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

sdiss: $(SDISS_OBJECTS) $(LIB)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

sin: $(SIN_OBJECTS) $(LIB)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

$(PARSER_GENERATED): $(PARSER_SOURCES)
	$(YACC) -o $(SRC_DIR)/parser.c --defines=$(SRC_DIR)/parser.h $<

$(LEXER_GENERATED): $(LEXER_SOURCES) $(PARSER_GENERATED)
	$(LEX) -o $(SRC_DIR)/lexer.c $<

# Make sure parser.o and lexer.o dependences are tracked
$(OBJ_DIR)/parser.o: $(SRC_DIR)/parser.c $(SRC_DIR)/parser.h
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(DEBUG) $< -o $@
$(OBJ_DIR)/lexer.o: $(SRC_DIR)/lexer.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(DEBUG) $< -o $@

# Include dependency files
-include $(DEPS)

clean:
	rm -rf $(OBJ_DIR)/*.o $(OBJ_DIR)/*.d $(LIB) $(LIB_DIR) \
         $(PARSER_GENERATED) $(LEXER_GENERATED)

