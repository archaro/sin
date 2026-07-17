CC = gcc
PKG_CONFIG ?= pkg-config
LIBUV_PC ?= libuv
# Default `make`/`make all` builds the debug variant. Override with
# `BUILD=release` or `BUILD=sanitize`, or use the variant targets below.
BUILD ?= debug
CSTD ?= c17

SRC_DIR := src
OBJ_DIR := obj
LIB_DIR := lib
GENERATED_DIR := $(OBJ_DIR)/generated

BASE_CFLAGS := -std=$(CSTD) -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -MMD -MP
CPPFLAGS := -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -I$(SRC_DIR) -I$(SRC_DIR)/common -I$(SRC_DIR)/bytecode -I$(SRC_DIR)/runtime -I$(SRC_DIR)/itemstore -I$(SRC_DIR)/libcall -I$(SRC_DIR)/net -I$(GENERATED_DIR)
DEBUG_CFLAGS := -g -O0 -DDEBUG=1
RELEASE_CFLAGS := -O2 -DNDEBUG
SANITIZE_CFLAGS := -g -O1 -DDEBUG=1
DEBUG_LDFLAGS := -g
RELEASE_LDFLAGS :=
SANITIZE_LDFLAGS := -g
LIBUV_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(LIBUV_PC) 2>/dev/null)
LIBUV_LIBS := $(shell $(PKG_CONFIG) --libs $(LIBUV_PC) 2>/dev/null || printf '%s' '-luv')
CFLAGS ?= $(BASE_CFLAGS)
LDFLAGS ?=
LIBS ?= $(LIBUV_LIBS)

.DEFAULT_GOAL := all

STRICT_WARNINGS ?= 0
ASAN_OPTIONS ?= strict_string_checks=1:abort_on_error=1
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
STRICT_WARNING_FLAGS := -Werror -Wshadow -Wformat=2 \
	-Wno-error=unused-parameter -Wno-error=sign-compare \
	-Wno-error=implicit-fallthrough -Wno-error=missing-field-initializers \
	-Wno-error=pedantic -Wno-error=shadow -Wno-error=type-limits \
	-Wno-error=format-nonliteral -Wno-error=conversion -Wno-error=sign-conversion
GENERATED_WARNING_FLAGS := -Wno-conversion -Wno-sign-conversion -Wno-pedantic

ifeq ($(BUILD),debug)
CFLAGS += $(DEBUG_CFLAGS)
LDFLAGS += $(DEBUG_LDFLAGS)
else ifeq ($(BUILD),release)
CFLAGS += $(RELEASE_CFLAGS)
LDFLAGS += $(RELEASE_LDFLAGS)
else ifeq ($(BUILD),sanitize)
CFLAGS += $(SANITIZE_CFLAGS)
LDFLAGS += $(SANITIZE_LDFLAGS)
else
$(error Unknown BUILD '$(BUILD)'; expected debug, release, or sanitize)
endif

CFLAGS += $(CPPFLAGS) $(LIBUV_CFLAGS)

ifeq ($(BUILD),sanitize)
CFLAGS += $(SANITIZE_FLAGS)
LDFLAGS += $(SANITIZE_FLAGS)
endif

ifeq ($(STRICT_WARNINGS),1)
CFLAGS += $(STRICT_WARNING_FLAGS)
GENERATED_WARNING_FLAGS += -Wno-error=format -Wno-error=format-nonliteral
endif
YACC = bison
LEX = flex

# Test runner
TEST_DIR := tests
TEST_BIN := $(TEST_DIR)/test-compiler
NETWORK_TEST_BIN := $(TEST_DIR)/network/test-network
CHAT_SMOKE_BIN := $(TEST_DIR)/network/test-chat-smoke
NETWORK_TEST_DEPS := $(NETWORK_TEST_BIN).d
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
	$(TEST_DIR)/core/test_cli_io.c \
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
LIB_OBJECTS := $(OBJ_DIR)/common/log.o $(OBJ_DIR)/common/memory.o $(OBJ_DIR)/common/cli_io.o $(OBJ_DIR)/bytecode/bytecode_verify.o $(OBJ_DIR)/bytecode/sdiss_core.o $(OBJ_DIR)/common/floatconv.o $(OBJ_DIR)/parser.o \
               $(OBJ_DIR)/lexer.o $(OBJ_DIR)/compiler/absyn.o $(OBJ_DIR)/compiler/semant.o \
               $(OBJ_DIR)/compiler/ir.o $(OBJ_DIR)/compiler/lower.o $(OBJ_DIR)/compiler/compiler_context.o $(OBJ_DIR)/compiler/compiler_pipeline.o $(OBJ_DIR)/compiler/emitbc.o \
               $(OBJ_DIR)/compiler/compdiag.o $(OBJ_DIR)/common/error.o $(OBJ_DIR)/common/util.o $(OBJ_DIR)/libcall/libcall_sys.o $(OBJ_DIR)/libcall/libcall_task.o $(OBJ_DIR)/libcall/libcall_net.o $(OBJ_DIR)/libcall/libcall_str.o $(OBJ_DIR)/libcall/libcall_registry.o $(OBJ_DIR)/libcall/libcall_table.o \
               $(OBJ_DIR)/runtime/stack.o $(OBJ_DIR)/runtime/value.o $(OBJ_DIR)/itemstore/item_hash.o $(OBJ_DIR)/itemstore/item_tree.o $(OBJ_DIR)/itemstore/item_registry.o $(OBJ_DIR)/itemstore/item_persist.o $(OBJ_DIR)/itemstore/item_error.o \
               $(OBJ_DIR)/runtime/vm.o $(OBJ_DIR)/runtime/task.o $(OBJ_DIR)/runtime/runtime_decode.o $(OBJ_DIR)/runtime/runtime_value.o $(OBJ_DIR)/runtime/runtime_item_ops.o $(OBJ_DIR)/runtime/runtime_opcode.o $(OBJ_DIR)/runtime/interpret.o \
               $(OBJ_DIR)/net/network.o $(OBJ_DIR)/net/libtelnet.o

# Parser files for library
PARSER_SOURCES := $(SRC_DIR)/compiler/parser.y
PARSER_C := $(GENERATED_DIR)/parser.c
PARSER_H := $(GENERATED_DIR)/parser.h
PARSER_GENERATED := $(PARSER_C) $(PARSER_H)

# Lexer files for library
LEXER_SOURCES := $(SRC_DIR)/compiler/lexer.l
LEXER_C := $(GENERATED_DIR)/lexer.c
LEXER_GENERATED := $(LEXER_C)

PROGRAMS := scomp sdiss sin
PROGRAM_OBJECTS := $(PROGRAMS:%=$(OBJ_DIR)/%.o)

# Dependency files
OBJECTS := $(LIB_OBJECTS) $(PROGRAM_OBJECTS)
DEPS := $(OBJECTS:.o=.d)

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $< -o $@

.PHONY: all lib clean help debug release sanitize
.PHONY: test test-network test-chat-smoke test-strict test-warnings test-asan test-lsan
.PHONY: fuzz-build fuzz-corpora fuzz-smoke fuzz-smoke-run
.PHONY: fuzz-scomp fuzz-sdiss fuzz-sin-object
.PHONY: seed-fuzz-sdiss-corpus seed-fuzz-sin-object-corpus

all: $(PROGRAMS)

lib: $(LIB)

debug:
	+$(MAKE) clean
	+$(MAKE) BUILD=debug all

release:
	+$(MAKE) clean
	+$(MAKE) BUILD=release all

sanitize:
	+$(MAKE) clean
	+ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=0" $(MAKE) BUILD=sanitize all

help:
	@printf '%s\n' \
		'Build targets:' \
		'  all              Build scomp, sdiss, and sin; default BUILD=debug' \
		'  debug            Clean, then build all with BUILD=debug' \
		'  release          Clean, then build all with BUILD=release' \
		'  sanitize         Clean, then build all with BUILD=sanitize and ASan/UBSan' \
		'  lib              Build lib/libsinshared.a only' \
		'  clean            Remove objects, binaries, libraries, tests, and stale generated files' \
		'' \
		'Test targets:' \
		'  test             Build debug artifacts and run network + standard suite' \
		'  test-network     Build and run network tests only' \
		'  test-chat-smoke  Run the real chat example through localhost' \
		'  test-strict      Run standard suite with benchmark budgets enabled' \
		'  test-warnings    Clean, rebuild, and test with STRICT_WARNINGS=1' \
		'  test-asan        Clean, rebuild, and test with BUILD=sanitize, leak checks off' \
		'  test-lsan        Clean, rebuild, and test with BUILD=sanitize, leak checks on' \
		'' \
		'Fuzz targets:' \
		'  fuzz-build       Clean and build all fuzz harnesses' \
		'  fuzz-corpora     Seed fuzz corpora from checked-in fixtures/examples' \
		'  fuzz-smoke       Build and run all seeded fuzz harnesses' \
		'  fuzz-smoke-run   Run already-built fuzz harnesses against seeded corpora' \
		'  fuzz-scomp       Build the scomp fuzz harness' \
		'  fuzz-sdiss       Build the sdiss fuzz harness' \
		'  fuzz-sin-object  Build the itemstore fuzz harness' \
		'' \
		'Common variables:' \
		'  BUILD=debug|release|sanitize  Select build variant; default debug' \
		'  CSTD=c17                      Select C standard passed as -std=$(CSTD)' \
		'  CC=gcc                        Select compiler' \
		'  PKG_CONFIG=pkg-config         Dependency discovery command' \
		'  LIBUV_PC=libuv                pkg-config module for libuv' \
		'  STRICT_WARNINGS=1             Promote selected warnings to errors' \
		'  FUZZ_CC=clang                 Compiler used by fuzz targets'

$(LIB): $(LIB_OBJECTS)
	@mkdir -p $(LIB_DIR)
	rm -f $@
	ar rcs $@ $^

scomp sdiss sin: %: $(OBJ_DIR)/%.o $(LIB)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

$(PARSER_GENERATED) &: $(PARSER_SOURCES)
	@mkdir -p $(GENERATED_DIR)
	$(YACC) -o $(PARSER_C) --defines=$(PARSER_H) $<

$(LEXER_GENERATED): $(LEXER_SOURCES) $(PARSER_GENERATED)
	@mkdir -p $(GENERATED_DIR)
	$(LEX) -o $(LEXER_C) $<

# Make sure parser.o and lexer.o dependences are tracked
$(OBJECTS): $(PARSER_H)

$(OBJ_DIR)/parser.o: $(PARSER_C) $(PARSER_H)
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(GENERATED_WARNING_FLAGS) $< -o $@
$(OBJ_DIR)/lexer.o: $(LEXER_C) $(PARSER_H)
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $(GENERATED_WARNING_FLAGS) $< -o $@

# Include dependency files
-include $(DEPS)
-include $(NETWORK_TEST_DEPS)

test: $(TEST_BIN) test-network test-chat-smoke
	./$(TEST_BIN)

test-network: $(NETWORK_TEST_BIN)
	./$(NETWORK_TEST_BIN)

test-chat-smoke: $(CHAT_SMOKE_BIN) scomp sin
	./$(CHAT_SMOKE_BIN)

test-strict: $(TEST_BIN)
	SIN_STRICT_BENCH=1 ./$(TEST_BIN)

test-warnings: clean
	+$(MAKE) STRICT_WARNINGS=1 test

test-asan: clean
	+ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=0" $(MAKE) BUILD=sanitize STRICT_WARNINGS=1 test

test-lsan: clean
	+ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=1" $(MAKE) BUILD=sanitize STRICT_WARNINGS=1 test

$(TEST_BIN): $(TEST_SOURCES) $(LIB) scomp sdiss sin
	$(CC) $(CFLAGS) -I$(TEST_DIR) -o $@ $(TEST_SOURCES) $(LIB) $(LDFLAGS) $(LIBS)

$(NETWORK_TEST_BIN): $(TEST_DIR)/network/test_network.c $(SRC_DIR)/net/network.c $(SRC_DIR)/net/network.h
	$(CC) $(CFLAGS) -I$(TEST_DIR) -MF $(NETWORK_TEST_DEPS) \
		-o $@ $(TEST_DIR)/network/test_network.c $(LDFLAGS) $(LIBS)

$(CHAT_SMOKE_BIN): $(TEST_DIR)/network/test_chat_smoke.c scomp sin
	$(CC) $(CFLAGS) -I$(TEST_DIR) -o $@ $(TEST_DIR)/network/test_chat_smoke.c

$(OBJ_DIR)/tests/fuzz/%.o : $(FUZZ_DIR)/%.c $(PARSER_GENERATED)
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $< -o $@

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
		$(NETWORK_TEST_BIN) $(CHAT_SMOKE_BIN) $(NETWORK_TEST_DEPS) \
		$(SRC_DIR)/parser.c $(SRC_DIR)/parser.h $(SRC_DIR)/lexer.c
