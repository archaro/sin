CC = gcc
CFLAGS = -g -Wall -MMD -MP -Isrc -Isrc/compiler
LDFLAGS = -g
LIBS = -luv

.DEFAULT_GOAL := all

SANITIZE ?= 0
STRICT_WARNINGS ?= 0
ASAN_OPTIONS ?= strict_string_checks=1:abort_on_error=1
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
STRICT_WARNING_FLAGS := -Wextra -Wpedantic -Werror -Wshadow -Wformat=2 \
	-Wno-error=unused-parameter -Wno-error=sign-compare \
	-Wno-error=implicit-fallthrough -Wno-error=missing-field-initializers \
	-Wno-error=pedantic -Wno-error=shadow -Wno-error=type-limits -Wno-error=format-nonliteral
GENERATED_WARNING_FLAGS :=

ifeq ($(SANITIZE),1)
CFLAGS += $(SANITIZE_FLAGS)
LDFLAGS += $(SANITIZE_FLAGS)
endif

ifeq ($(STRICT_WARNINGS),1)
CFLAGS += $(STRICT_WARNING_FLAGS)
GENERATED_WARNING_FLAGS += -Wno-error=format -Wno-error=format-nonliteral
endif
YACC = bison
LEX = flex
DEBUG = -DDEBUG=1 #-DSTRINGDEBUG=1 -DDISASS=1

SRC_DIR := src
OBJ_DIR := obj
LIB_DIR := lib

# Test runner
TEST_DIR := tests
TEST_BIN := $(TEST_DIR)/test-compiler
NETWORK_TEST_BIN := $(TEST_DIR)/network/test-network
FUZZ_CC ?= clang
FUZZ_TIME ?= 30
FUZZ_RUNS ?= 10000
FUZZ_SEED ?= 1
FUZZ_DIR := $(TEST_DIR)/fuzz
FUZZ_BIN := $(FUZZ_DIR)/fuzz_scomp
FUZZ_CORPUS_DIR := $(FUZZ_DIR)/corpus/scomp
FUZZ_SDISS_BIN := $(FUZZ_DIR)/fuzz_sdiss
FUZZ_SDISS_CORPUS_DIR := $(FUZZ_DIR)/corpus/sdiss
FUZZ_SIN_OBJECT_BIN := $(FUZZ_DIR)/fuzz_sin_object
FUZZ_SIN_OBJECT_CORPUS_DIR := $(FUZZ_DIR)/corpus/sin-object
FUZZ_BINS := $(FUZZ_BIN) $(FUZZ_SDISS_BIN) $(FUZZ_SIN_OBJECT_BIN)
FUZZ_SANITIZE_FLAGS := -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
FUZZ_LINK_FLAGS := -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
FUZZ_CFLAGS ?= $(CFLAGS) $(FUZZ_SANITIZE_FLAGS)
FUZZ_LDFLAGS ?= $(LDFLAGS) $(FUZZ_SANITIZE_FLAGS)
FUZZ_MAKE = $(MAKE) CC="$(FUZZ_CC)" CFLAGS="$(FUZZ_CFLAGS)" LDFLAGS="$(FUZZ_LDFLAGS)"
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
	$(TEST_DIR)/core/test_itemstore_verifier.c \
	$(TEST_DIR)/core/test_libcall_registry.c \
	$(TEST_DIR)/core/test_relative_item_leading_dot.c \
	$(TEST_DIR)/core/test_value_behavior.c
TEST_COMPILER_SOURCES := \
	$(TEST_DIR)/compiler/test_emitbc_header.c \
	$(TEST_DIR)/compiler/test_emitbc_opcode_map.c \
	$(TEST_DIR)/compiler/test_emitbc_all_ir_ops_accounted_for.c \
	$(TEST_DIR)/compiler/test_emitbc_jumps.c \
	$(TEST_DIR)/compiler/test_emitbc_invariants.c \
	$(TEST_DIR)/compiler/test_emitbc_post_verify.c \
	$(TEST_DIR)/compiler/test_bytecode_verify.c \
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
LIB_OBJECTS := $(OBJ_DIR)/log.o $(OBJ_DIR)/memory.o $(OBJ_DIR)/bytecode_verify.o $(OBJ_DIR)/sdiss_core.o $(OBJ_DIR)/floatconv.o $(OBJ_DIR)/parser.o \
               $(OBJ_DIR)/lexer.o $(OBJ_DIR)/compiler/absyn.o $(OBJ_DIR)/compiler/semant.o \
               $(OBJ_DIR)/compiler/ir.o $(OBJ_DIR)/compiler/lower.o $(OBJ_DIR)/compiler/compiler_context.o $(OBJ_DIR)/compiler/compiler_pipeline.o $(OBJ_DIR)/compiler/emitbc.o \
               $(OBJ_DIR)/compiler/compdiag.o $(OBJ_DIR)/error.o $(OBJ_DIR)/util.o $(OBJ_DIR)/libcall.o $(OBJ_DIR)/libcall_registry.o $(OBJ_DIR)/libcall_table.o \
               $(OBJ_DIR)/stack.o $(OBJ_DIR)/value.o $(OBJ_DIR)/item.o $(OBJ_DIR)/item_hash.o $(OBJ_DIR)/item_tree.o $(OBJ_DIR)/item_registry.o $(OBJ_DIR)/item_persist.o $(OBJ_DIR)/item_error.o \
               $(OBJ_DIR)/vm.o $(OBJ_DIR)/task.o $(OBJ_DIR)/runtime_decode.o $(OBJ_DIR)/runtime_value.o $(OBJ_DIR)/runtime_item_ops.o $(OBJ_DIR)/runtime_opcode.o $(OBJ_DIR)/interpret.o \
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

$(OBJ_DIR)/scomp.o: $(PARSER_GENERATED)

# Source files for sdiss
SDISS_SOURCES := $(SRC_DIR)/sdiss.c
SDISS_OBJECTS := $(SDISS_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Source files for sin
SIN_SOURCES := $(SRC_DIR)/sin.c
SIN_OBJECTS := $(SIN_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
PROGRAMS := scomp sdiss sin

# Dependency files
OBJECTS := $(LIB_OBJECTS) $(SCOMP_OBJECTS) $(SDISS_OBJECTS) $(SIN_OBJECTS)
DEPS := $(OBJECTS:.o=.d)

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(DEBUG) $< -o $@

.PHONY: all lib clean help
.PHONY: test test-network test-strict test-warnings test-asan test-lsan
.PHONY: fuzz-build fuzz-corpora fuzz-smoke fuzz-smoke-run
.PHONY: fuzz-scomp fuzz-sdiss fuzz-sin-object
.PHONY: seed-fuzz-sdiss-corpus seed-fuzz-sin-object-corpus

all: $(PROGRAMS)

lib: $(LIB)

help:
	@printf '%s\n' \
		'Build targets:' \
		'  all              Build scomp, sdiss, and sin (default)' \
		'  clean            Remove generated and compiled files' \
		'' \
		'Test targets:' \
		'  test             Run the standard test suite' \
		'  test-strict      Run tests with benchmark budgets enabled' \
		'  test-warnings    Clean, rebuild, and test with strict warnings' \
		'  test-asan        Clean, rebuild, and test with ASan/UBSan' \
		'  test-lsan        Clean, rebuild, and test with leak detection' \
		'' \
		'Fuzz targets:' \
		'  fuzz-build       Clean and build all fuzz harnesses' \
		'  fuzz-smoke       Build and run all seeded fuzz harnesses' \
		'  fuzz-scomp       Build the scomp fuzz harness' \
		'  fuzz-sdiss       Build the sdiss fuzz harness' \
		'  fuzz-sin-object  Build the itemstore fuzz harness'

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
	$(CC) -c $(CFLAGS) $(GENERATED_WARNING_FLAGS) $(DEBUG) $< -o $@
$(OBJ_DIR)/lexer.o: $(SRC_DIR)/lexer.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(GENERATED_WARNING_FLAGS) $(DEBUG) $< -o $@

# Include dependency files
-include $(DEPS)

test: $(TEST_BIN) test-network
	./$(TEST_BIN)

test-network: $(NETWORK_TEST_BIN)
	./$(NETWORK_TEST_BIN)

test-strict: $(TEST_BIN)
	SIN_STRICT_BENCH=1 ./$(TEST_BIN)

test-warnings: clean
	+$(MAKE) STRICT_WARNINGS=1 test

test-asan: clean
	+ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=0" $(MAKE) SANITIZE=1 STRICT_WARNINGS=1 test

test-lsan: clean
	+ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=1" $(MAKE) SANITIZE=1 STRICT_WARNINGS=1 test

$(TEST_BIN): $(TEST_SOURCES) $(LIB) scomp sdiss sin
	$(CC) $(CFLAGS) $(DEBUG) -Isrc -I$(TEST_DIR) -o $@ $(TEST_SOURCES) $(LIB) $(LDFLAGS) $(LIBS)

$(NETWORK_TEST_BIN): $(TEST_DIR)/network/test_network.c $(SRC_DIR)/network.c $(SRC_DIR)/network.h
	$(CC) $(CFLAGS) $(DEBUG) -Isrc -I$(TEST_DIR) -o $@ $(TEST_DIR)/network/test_network.c $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/tests/fuzz/%.o : $(FUZZ_DIR)/%.c $(PARSER_GENERATED)
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(DEBUG) $< -o $@

$(FUZZ_BIN): $(OBJ_DIR)/tests/fuzz/fuzz_scomp.o $(LIB)
	$(CC) -o $@ $^ $(FUZZ_LINK_FLAGS) $(LIBS)

$(FUZZ_SDISS_BIN): $(OBJ_DIR)/tests/fuzz/fuzz_sdiss.o $(LIB)
	$(CC) -o $@ $^ $(FUZZ_LINK_FLAGS) $(LIBS)

$(FUZZ_SIN_OBJECT_BIN): $(OBJ_DIR)/tests/fuzz/fuzz_sin_object.o $(LIB)
	$(CC) -o $@ $^ $(FUZZ_LINK_FLAGS) $(LIBS)

seed-fuzz-sdiss-corpus:
	@mkdir -p $(FUZZ_SDISS_CORPUS_DIR)
	@if command -v xxd >/dev/null 2>&1; then \
		for hex in $(TEST_DIR)/fixtures/sdiss/*.hex; do \
			[ -e "$$hex" ] || continue; \
			xxd -r -p "$$hex" "$(FUZZ_SDISS_CORPUS_DIR)/$$(basename "$$hex" .hex).obj"; \
		done; \
	else \
		printf 'xxd not found; skipping sdiss fixture corpus seeding\n'; \
	fi

fuzz-corpora: seed-fuzz-sdiss-corpus seed-fuzz-sin-object-corpus

fuzz-build: clean
	+$(FUZZ_MAKE) fuzz-corpora $(FUZZ_BINS)

fuzz-smoke: fuzz-build
	+$(MAKE) fuzz-smoke-run

# Runs the already-built fuzz harnesses against seeded corpora.
fuzz-smoke-run:
	@set -eu; \
	for bin in $(FUZZ_BINS); do \
		[ -x "$$bin" ] || { printf 'Missing %s; run make fuzz-build first.\n' "$$bin" >&2; exit 1; }; \
	done; \
	tmp="$$(mktemp -d)"; \
	trap 'rm -rf "$$tmp"' EXIT; \
	mkdir -p "$$tmp/scomp" "$$tmp/sdiss" "$$tmp/sin-object"; \
	$(FUZZ_BIN) -runs=$(FUZZ_RUNS) -max_total_time=$(FUZZ_TIME) -seed=$(FUZZ_SEED) "$$tmp/scomp" $(FUZZ_CORPUS_DIR); \
	$(FUZZ_SDISS_BIN) -runs=$(FUZZ_RUNS) -max_total_time=$(FUZZ_TIME) -seed=$(FUZZ_SEED) "$$tmp/sdiss" $(FUZZ_SDISS_CORPUS_DIR); \
	$(FUZZ_SIN_OBJECT_BIN) -runs=$(FUZZ_RUNS) -max_total_time=$(FUZZ_TIME) -seed=$(FUZZ_SEED) "$$tmp/sin-object" $(FUZZ_SIN_OBJECT_CORPUS_DIR)

fuzz-scomp: clean
	+$(FUZZ_MAKE) $(FUZZ_BIN)
	@printf 'Built %s. Run with: %s %s\n' "$(FUZZ_BIN)" "$(FUZZ_BIN)" "$(FUZZ_CORPUS_DIR)"

fuzz-sdiss: clean
	+$(FUZZ_MAKE) seed-fuzz-sdiss-corpus $(FUZZ_SDISS_BIN)
	@printf 'Built %s. Run with: %s %s\n' "$(FUZZ_SDISS_BIN)" "$(FUZZ_SDISS_BIN)" "$(FUZZ_SDISS_CORPUS_DIR)"

seed-fuzz-sin-object-corpus: scomp
	@mkdir -p $(FUZZ_SIN_OBJECT_CORPUS_DIR)
	@for src in examples/chat-boot.src examples/chat-load.src examples/echo-boot.src examples/echo-load.src; do \
		[ -e "$$src" ] || continue; \
		obj="$(FUZZ_SIN_OBJECT_CORPUS_DIR)/$$(basename "$$src" .src).obj"; \
		./scomp "$$src" "$$obj" >/dev/null 2>&1 || rm -f "$$obj"; \
	done

# Builds a libFuzzer harness for sin object/itemstore loading and strict bytecode validation.
fuzz-sin-object: clean
	+$(FUZZ_MAKE) seed-fuzz-sin-object-corpus $(FUZZ_SIN_OBJECT_BIN)
	@printf 'Built %s. Run with: %s %s\n' "$(FUZZ_SIN_OBJECT_BIN)" "$(FUZZ_SIN_OBJECT_BIN)" "$(FUZZ_SIN_OBJECT_CORPUS_DIR)"

clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR) $(PROGRAMS) $(TEST_BIN) $(FUZZ_BINS) \
		$(NETWORK_TEST_BIN) $(PARSER_GENERATED) $(LEXER_GENERATED)
