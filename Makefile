CC = gcc
CFLAGS = -g -Wall -MMD -MP
LDFLAGS = -g
LIBS = -luv
YACC = bison
LEX = flex
DEBUG = -DDEBUG=1 #-DSTRINGDEBUG=1 -DDISASS=1

SRC_DIR := src
OBJ_DIR := obj
LIB_DIR := lib

# Test runner
TEST_DIR := tests
TEST_BIN := $(TEST_DIR)/test-compiler
TEST_SOURCES := $(TEST_DIR)/test_compiler.c $(TEST_DIR)/test_helpers.c $(TEST_DIR)/test_emitbc_header.c $(TEST_DIR)/test_emitbc_opcode_map.c $(TEST_DIR)/test_emitbc_jumps.c $(TEST_DIR)/test_pipeline_golden.c $(TEST_DIR)/test_pipeline_source_golden.c $(TEST_DIR)/test_ir_validate.c $(TEST_DIR)/test_absyn_lifecycle.c $(TEST_DIR)/test_semant.c


# Library of shared functions
LIB := $(LIB_DIR)/libsinshared.a
LIB_OBJECTS := $(OBJ_DIR)/log.o $(OBJ_DIR)/memory.o \
               $(OBJ_DIR)/parser.o $(OBJ_DIR)/lexer.o $(OBJ_DIR)/absyn.o \
               $(OBJ_DIR)/semant.o $(OBJ_DIR)/ir.o $(OBJ_DIR)/lower.o \
               $(OBJ_DIR)/emitbc.o $(OBJ_DIR)/error.o $(OBJ_DIR)/util.o $(OBJ_DIR)/libcall.o \
               $(OBJ_DIR)/stack.o $(OBJ_DIR)/value.o $(OBJ_DIR)/item.o \
               $(OBJ_DIR)/vm.o $(OBJ_DIR)/task.o $(OBJ_DIR)/interpret.o \
               $(OBJ_DIR)/network.o $(OBJ_DIR)/libtelnet.o

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
SIN_SOURCES := $(SRC_DIR)/sin.c
SIN_OBJECTS := $(SIN_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Dependency files
OBJECTS := $(LIB_OBJECTS) $(SCOMP_OBJECTS) $(SDISS_OBJECTS) $(SIN_OBJECTS)
DEPS := $(OBJECTS:.o=.d)

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(DEBUG) $< -o $@

.PHONY: all clean lib test-compiler

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

test-compiler: $(TEST_BIN)

$(TEST_BIN): $(TEST_SOURCES) $(LIB)
	$(CC) $(CFLAGS) $(DEBUG) -Isrc -o $@ $(TEST_SOURCES) $(LIB) $(LDFLAGS) $(LIBS)

clean:
	rm -rf $(OBJ_DIR)/*.o $(OBJ_DIR)/*.d $(LIB) $(LIB_DIR) \
         $(PARSER_GENERATED) $(LEXER_GENERATED) $(TEST_BIN)

