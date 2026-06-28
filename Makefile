CC = gcc
CFLAGS = -g -Wall -MMD -MP -Isrc -Isrc/compiler
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
TEST_SHARED_SOURCES := \
	$(TEST_DIR)/shared/test_compiler.c \
	$(TEST_DIR)/shared/test_helpers.c \
	$(TEST_DIR)/shared/test_fixture_policy.c \
	$(TEST_DIR)/shared/test_pipeline_cases.c
TEST_CORE_SOURCES := \
	$(TEST_DIR)/core/test_absyn_lifecycle.c \
	$(TEST_DIR)/core/test_semant.c \
	$(TEST_DIR)/core/test_ir_validate.c \
	$(TEST_DIR)/core/test_opcode_schema.c \
	$(TEST_DIR)/core/test_parser_input_api.c \
	$(TEST_DIR)/core/test_parser_float_literals.c \
	$(TEST_DIR)/core/test_item_cache.c \
	$(TEST_DIR)/core/test_itemstore_io.c \
	$(TEST_DIR)/core/test_libcall_registry.c \
	$(TEST_DIR)/core/test_relative_item_leading_dot.c \
	$(TEST_DIR)/core/test_value_behavior.c
TEST_COMPILER_SOURCES := \
	$(TEST_DIR)/compiler/test_emitbc_header.c \
	$(TEST_DIR)/compiler/test_emitbc_opcode_map.c \
	$(TEST_DIR)/compiler/test_emitbc_all_ir_ops_accounted_for.c \
	$(TEST_DIR)/compiler/test_emitbc_jumps.c \
	$(TEST_DIR)/compiler/test_emitbc_invariants.c \
	$(TEST_DIR)/compiler/test_pipeline_golden.c \
	$(TEST_DIR)/compiler/test_pipeline_source_golden.c \
	$(TEST_DIR)/compiler/test_pipeline_negative_matrix.c \
	$(TEST_DIR)/compiler/test_parser_examples_obj_golden.c \
	$(TEST_DIR)/compiler/test_sdiss_fixtures.c \
	$(TEST_DIR)/compiler/test_compiler_context_failures.c \
	$(TEST_DIR)/compiler/test_compiler_diag_pipeline.c \
	$(TEST_DIR)/compiler/test_sys_compile_libcall.c
TEST_INTERPRETER_SOURCES := \
	$(TEST_DIR)/interpreter/test_interpret_semantics_golden.c \
	$(TEST_DIR)/interpreter/test_interpret_stress.c \
	$(TEST_DIR)/interpreter/test_runtime_benchmark_optin.c
TEST_SOURCES := $(TEST_SHARED_SOURCES) $(TEST_CORE_SOURCES) $(TEST_COMPILER_SOURCES) $(TEST_INTERPRETER_SOURCES)


# Library of shared functions
LIB := $(LIB_DIR)/libsinshared.a
LIB_OBJECTS := $(OBJ_DIR)/log.o $(OBJ_DIR)/memory.o $(OBJ_DIR)/bytecode_verify.o $(OBJ_DIR)/floatconv.o $(OBJ_DIR)/parser.o \
               $(OBJ_DIR)/lexer.o $(OBJ_DIR)/compiler/absyn.o $(OBJ_DIR)/compiler/semant.o \
               $(OBJ_DIR)/compiler/ir.o $(OBJ_DIR)/compiler/lower.o $(OBJ_DIR)/compiler/compiler_context.o $(OBJ_DIR)/compiler/compiler_pipeline.o $(OBJ_DIR)/compiler/emitbc.o \
               $(OBJ_DIR)/compiler/compdiag.o $(OBJ_DIR)/error.o $(OBJ_DIR)/util.o $(OBJ_DIR)/libcall.o \
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

.PHONY: all clean lib test teststrict

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

$(PARSER_GENERATED) &: $(PARSER_SOURCES)
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

test: $(TEST_BIN)
	./$(TEST_BIN)

teststrict: $(TEST_BIN)
	SIN_STRICT_BENCH=1 ./$(TEST_BIN)

$(TEST_BIN): $(TEST_SOURCES) $(LIB) scomp sdiss sin
	$(CC) $(CFLAGS) $(DEBUG) -Isrc -I$(TEST_DIR) -o $@ $(TEST_SOURCES) $(LIB) $(LDFLAGS) $(LIBS)

clean:
	rm -rf $(OBJ_DIR)/*.o $(OBJ_DIR)/*.d $(LIB) $(LIB_DIR) \
         $(PARSER_GENERATED) $(LEXER_GENERATED) $(TEST_BIN)
