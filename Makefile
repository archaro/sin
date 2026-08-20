CC = gcc
PKG_CONFIG ?= pkg-config
LIBUV_PC ?= libuv
# Default `make`/`make all` builds the debug variant. Override with
# `BUILD=release` or `BUILD=sanitize`, or use the variant targets below.
BUILD ?= debug
CSTD ?= c17

SRC_DIR := src
BUILD_TAG := $(BUILD)-$(notdir $(CC))
OBJ_DIR := obj/$(BUILD_TAG)
LIB_DIR := lib/$(BUILD_TAG)
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
LIBS ?= $(LIBUV_LIBS) -lm

.DEFAULT_GOAL := all

STRICT_WARNINGS ?= 0
ASAN_OPTIONS ?= strict_string_checks=1:abort_on_error=1
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
STRICT_WARNING_FLAGS := -Werror -Wshadow -Wformat=2 \
	-Wno-error=unused-parameter -Wno-error=sign-compare \
	-Wno-error=implicit-fallthrough -Wno-error=missing-field-initializers \
	-Wno-error=shadow -Wno-error=type-limits
GENERATED_WARNING_FLAGS := -Wno-conversion -Wno-sign-conversion -Wno-pedantic \
	-Wno-unused-but-set-variable

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

CFLAGS += $(LIBUV_CFLAGS)

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
TEST_BIN := $(TEST_DIR)/test-suite
NETWORK_TEST_BIN := $(TEST_DIR)/network/test-network
CHAT_SMOKE_BIN := $(TEST_DIR)/network/test-chat-smoke
TEST_BINS := $(TEST_BIN) $(NETWORK_TEST_BIN) $(CHAT_SMOKE_BIN)
TEST_DEPS := $(TEST_BINS:%=%.d)
TEST_TMP_ARTIFACTS := $(TEST_DIR)/fixtures/sdiss/*.bin
TEST_CFLAGS = $(filter-out -MMD -MP,$(CFLAGS))
QUIET_RUNNER := $(TEST_DIR)/shared/quiet_runner.sh
QUIET_OUTPUT_TEST := $(TEST_DIR)/test_quiet_output.sh
FRAMEWORK_DIR := $(TEST_DIR)/framework
FRAMEWORK_SELF_BIN := $(OBJ_DIR)/$(FRAMEWORK_DIR)/framework-selftest
FRAMEWORK_RUNNER_BIN := $(OBJ_DIR)/$(FRAMEWORK_DIR)/framework-runner
FRAMEWORK_DUP_BIN := $(OBJ_DIR)/$(FRAMEWORK_DIR)/framework-duplicate-fixture
FRAMEWORK_NEG_BIN := $(OBJ_DIR)/$(FRAMEWORK_DIR)/framework-negative-fixture
CONFORMANCE_DIR := $(TEST_DIR)/conformance
CONFORMANCE_BIN := $(OBJ_DIR)/$(CONFORMANCE_DIR)/test-conformance
CONFORMANCE_MANIFEST := $(TEST_DIR)/fixtures/conformance/conformance.manifest
CONFORMANCE_FIXTURES := $(CONFORMANCE_MANIFEST) $(wildcard $(TEST_DIR)/fixtures/conformance/*.src $(TEST_DIR)/fixtures/conformance/*.txt $(TEST_DIR)/fixtures/conformance/negative/*.src $(TEST_DIR)/fixtures/conformance/negative/*.txt)
FRAMEWORK_SOURCES := $(FRAMEWORK_DIR)/test_framework.c $(FRAMEWORK_DIR)/framework_config.c
FRAMEWORK_SELF_SOURCES := $(FRAMEWORK_DIR)/framework_selftest.c
FRAMEWORK_RUNNER_SOURCES := $(FRAMEWORK_DIR)/test_runner.c
FRAMEWORK_DUP_SOURCES := $(FRAMEWORK_DIR)/framework_duplicate_fixture.c
FRAMEWORK_NEG_SOURCES := $(FRAMEWORK_DIR)/framework_negative_fixture.c
REWRITE_GROUP1_DIR := $(TEST_DIR)/rewrite/group1
REWRITE_GROUP2_DIR := $(TEST_DIR)/rewrite
REWRITE_GROUP1_SHARED_SOURCES := \
	$(TEST_DIR)/shared/test_helpers.c \
	$(TEST_DIR)/shared/test_libcall_support.c \
	$(TEST_DIR)/shared/test_pipeline_cases.c
REWRITE_GROUP1_HEADERS := \
	$(FRAMEWORK_DIR)/test_framework.h $(TEST_DIR)/test_assert.h \
	$(SRC_DIR)/config.h $(SRC_DIR)/itemstore/item.h $(SRC_DIR)/itemstore/item_internal.h \
	$(SRC_DIR)/common/memory.h $(PARSER_H)
REWRITE_GROUP1_LINK_SOURCES := $(FRAMEWORK_SOURCES) $(REWRITE_GROUP1_SHARED_SOURCES)
REWRITE_GROUP1_COMMON_DEPS := $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP1_HEADERS) $(LIB)
REWRITE_GROUP1_BINS := \
	$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_absyn_lifecycle \
	$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_parser_input_api \
	$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_cli_io \
	$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_parser_float_literals \
	$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_sconv \
	$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_value_behavior \
	$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_libcall_registry \
	$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_libcall_sys \
	$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_fixture_policy \
	$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_output_contract \
	$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_memory
REWRITE_GROUP2_BINS := \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_semant \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_ir_validate \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_relative_item_leading_dot \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_pipeline_golden \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_pipeline_source_golden \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_pipeline_negative_matrix \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_parser_examples_obj_golden \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_compiler_context_failures \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_compiler_diag_pipeline
REWRITE_GROUP3_BINS := \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_opcode_schema \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_bytecode_convert \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_bytecode_v1_abi \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_bytecode_verify \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_bytecode_wire \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_all_ir_ops \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_header \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_invariants \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_jumps \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_opcode_map \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_post_verify \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_sdiss_fixtures
REWRITE_GROUP4_BINS := \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_stack_frames \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_list \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_interpret_semantics \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_interpret_stress \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_runtime_benchmark
REWRITE_GROUP5_BINS := \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_item_cache \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_itemstore_io \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_sin_itemstore_policy
REWRITE_GROUP6_BINS := \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_libcall_task \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_task_lifecycle \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_libcall_net \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_libcall_str \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_libcall_list \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_libcall_sys_compile
REWRITE_GROUP7_BINS := \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_network \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_chat_smoke
REWRITE_GROUP8_BINS := \
	$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_cli_contract_matrix
FRAMEWORK_BINS := $(FRAMEWORK_SELF_BIN) $(FRAMEWORK_RUNNER_BIN) $(FRAMEWORK_DUP_BIN) $(FRAMEWORK_NEG_BIN) $(CONFORMANCE_BIN) $(REWRITE_GROUP1_BINS) $(REWRITE_GROUP2_BINS) $(REWRITE_GROUP3_BINS) $(REWRITE_GROUP4_BINS) $(REWRITE_GROUP5_BINS) $(REWRITE_GROUP6_BINS) $(REWRITE_GROUP7_BINS) $(REWRITE_GROUP8_BINS)
FUZZ_CC ?= clang
FUZZ_TIME ?= 30
FUZZ_RUNS ?= 10000
FUZZ_SEED ?= 1
FUZZ_ARTIFACT_DIR ?=
XXD ?= xxd
BEAR ?= bear
COMPILEDB := compile_commands.json
FUZZ_DIR := $(TEST_DIR)/fuzz
FUZZ_LOCAL_ARTIFACT_DIR := $(FUZZ_DIR)/artifacts
FUZZ_BIN := $(FUZZ_DIR)/fuzz_scomp
FUZZ_CORPUS_DIR := $(FUZZ_DIR)/corpus/scomp
FUZZ_SDISS_BIN := $(FUZZ_DIR)/fuzz_sdiss
FUZZ_SDISS_CORPUS_DIR := $(FUZZ_DIR)/corpus/sdiss
FUZZ_SIN_OBJECT_BIN := $(FUZZ_DIR)/fuzz_sin_object
FUZZ_SIN_OBJECT_CORPUS_DIR := $(FUZZ_DIR)/corpus/sin-object
FUZZ_BINS := $(FUZZ_BIN) $(FUZZ_SDISS_BIN) $(FUZZ_SIN_OBJECT_BIN)
GENERATED_FUZZ_CORPUS := $(FUZZ_SDISS_CORPUS_DIR)/*.obj $(FUZZ_SIN_OBJECT_CORPUS_DIR)/*.obj $(FUZZ_SIN_OBJECT_CORPUS_DIR)/*.itemstore
FUZZ_SANITIZE_FLAGS := -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
FUZZ_LINK_FLAGS := -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
FUZZ_CFLAGS ?= $(CFLAGS) $(FUZZ_SANITIZE_FLAGS)
FUZZ_LDFLAGS ?= $(LDFLAGS) $(FUZZ_SANITIZE_FLAGS)
FUZZ_MAKE = $(MAKE) CC="$(FUZZ_CC)" CFLAGS="$(FUZZ_CFLAGS)" LDFLAGS="$(FUZZ_LDFLAGS)"
TEST_SHARED_SOURCES := \
	$(TEST_DIR)/shared/test_harness.c \
	$(TEST_DIR)/shared/test_helpers.c \
	$(TEST_DIR)/shared/test_libcall_support.c \
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
	$(TEST_DIR)/core/test_libcall_registry.c \
	$(TEST_DIR)/core/test_libcall_sys.c \
	$(TEST_DIR)/core/test_libcall_task.c \
	$(TEST_DIR)/core/test_libcall_net.c \
	$(TEST_DIR)/core/test_libcall_str.c \
	$(TEST_DIR)/core/test_libcall_list.c \
	$(TEST_DIR)/core/test_libcall_sys_compile.c \
	$(TEST_DIR)/core/test_task_lifecycle.c \
	$(TEST_DIR)/core/test_relative_item_leading_dot.c \
	$(TEST_DIR)/core/test_stack_frames.c \
	$(TEST_DIR)/core/test_value_behavior.c \
	$(TEST_DIR)/core/test_list.c \
	$(TEST_DIR)/core/test_sconv.c \
	$(TEST_DIR)/core/test_sin_itemstore_policy.c
TEST_COMPILER_SOURCES := \
	$(TEST_DIR)/compiler/test_emitbc_header.c \
	$(TEST_DIR)/compiler/test_emitbc_opcode_map.c \
	$(TEST_DIR)/compiler/test_emitbc_all_ir_ops_accounted_for.c \
	$(TEST_DIR)/compiler/test_bytecode_v1_abi.c \
	$(TEST_DIR)/compiler/test_emitbc_jumps.c \
	$(TEST_DIR)/compiler/test_emitbc_invariants.c \
	$(TEST_DIR)/compiler/test_emitbc_post_verify.c \
	$(TEST_DIR)/compiler/test_bytecode_verify.c \
	$(TEST_DIR)/compiler/test_bytecode_wire.c \
	$(TEST_DIR)/compiler/test_bytecode_convert.c \
	$(TEST_DIR)/compiler/test_pipeline_golden.c \
	$(TEST_DIR)/compiler/test_pipeline_source_golden.c \
	$(TEST_DIR)/compiler/test_pipeline_negative_matrix.c \
	$(TEST_DIR)/compiler/test_parser_examples_obj_golden.c \
	$(TEST_DIR)/compiler/test_sdiss_fixtures.c \
	$(TEST_DIR)/compiler/test_compiler_context_failures.c \
	$(TEST_DIR)/compiler/test_compiler_diag_pipeline.c

TEST_INTERPRETER_SOURCES := \
	$(TEST_DIR)/interpreter/test_interpret_semantics_golden.c \
	$(TEST_DIR)/interpreter/test_interpret_stress.c \
	$(TEST_DIR)/interpreter/test_runtime_benchmark_optin.c
TEST_SOURCES := $(TEST_SHARED_SOURCES) $(TEST_CORE_SOURCES) $(TEST_COMPILER_SOURCES) $(TEST_INTERPRETER_SOURCES)


# Library of shared functions
LIB := $(LIB_DIR)/libsinshared.a
LIB_OBJECTS := $(OBJ_DIR)/common/log.o $(OBJ_DIR)/common/memory.o $(OBJ_DIR)/common/cli_io.o $(OBJ_DIR)/bytecode/bytecode_abi.o $(OBJ_DIR)/bytecode/bytecode_wire.o $(OBJ_DIR)/bytecode/bytecode_format.o $(OBJ_DIR)/bytecode/bytecode_verify.o $(OBJ_DIR)/bytecode/bytecode_convert.o $(OBJ_DIR)/bytecode/sdiss_core.o $(OBJ_DIR)/common/floatconv.o $(OBJ_DIR)/parser.o \
               $(OBJ_DIR)/lexer.o $(OBJ_DIR)/compiler/absyn.o $(OBJ_DIR)/compiler/semant.o \
               $(OBJ_DIR)/compiler/ir.o $(OBJ_DIR)/compiler/lower.o $(OBJ_DIR)/compiler/compiler_context.o $(OBJ_DIR)/compiler/compiler_pipeline.o $(OBJ_DIR)/compiler/emitbc.o \
               $(OBJ_DIR)/compiler/compdiag.o $(OBJ_DIR)/common/error.o $(OBJ_DIR)/common/util.o $(OBJ_DIR)/libcall/libcall_sys.o $(OBJ_DIR)/libcall/libcall_task.o $(OBJ_DIR)/libcall/libcall_net.o $(OBJ_DIR)/libcall/libcall_str.o $(OBJ_DIR)/libcall/libcall_list.o $(OBJ_DIR)/libcall/libcall_registry.o $(OBJ_DIR)/libcall/libcall_table.o \
               $(OBJ_DIR)/runtime/stack.o $(OBJ_DIR)/runtime/value.o $(OBJ_DIR)/runtime/list.o $(OBJ_DIR)/runtime/itemref.o $(OBJ_DIR)/itemstore/item_hash.o $(OBJ_DIR)/itemstore/item_tree.o $(OBJ_DIR)/itemstore/item_registry.o $(OBJ_DIR)/itemstore/item_persist.o $(OBJ_DIR)/itemstore/item_source_persist.o $(OBJ_DIR)/itemstore/item_error.o \
               $(OBJ_DIR)/itemstore/item_persist_v1.o $(OBJ_DIR)/itemstore/item_persist_v2.o $(OBJ_DIR)/runtime/vm.o $(OBJ_DIR)/runtime/task.o $(OBJ_DIR)/runtime/runtime_decode.o $(OBJ_DIR)/runtime/runtime_value.o $(OBJ_DIR)/runtime/runtime_item_ops.o $(OBJ_DIR)/runtime/runtime_opcode.o $(OBJ_DIR)/runtime/runtime_frame.o $(OBJ_DIR)/runtime/interpret.o \
               $(OBJ_DIR)/net/network.o $(OBJ_DIR)/net/libtelnet.o

# Parser files for library
PARSER_SOURCES := $(SRC_DIR)/compiler/parser.y
PARSER_C := $(GENERATED_DIR)/parser.c
PARSER_H := $(GENERATED_DIR)/parser.h
PARSER_GENERATED := $(PARSER_C) $(PARSER_H)
.SECONDARY: $(PARSER_GENERATED)

# Lexer files for library
LEXER_SOURCES := $(SRC_DIR)/compiler/lexer.l
LEXER_C := $(GENERATED_DIR)/lexer.c
LEXER_GENERATED := $(LEXER_C)

PROGRAMS := scomp sdiss sin sconv
PROGRAM_OBJECTS := $(PROGRAMS:%=$(OBJ_DIR)/%.o)

# Dependency files
OBJECTS := $(LIB_OBJECTS) $(PROGRAM_OBJECTS)
DEPS := $(OBJECTS:.o=.d)

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

.PHONY: all lib clean help debug release sanitize compiledb FORCE_BUILD
.PHONY: test test-framework test-conformance framework-list inventory-audit inventory-audit-self-test test-network test-chat-smoke test-output-contract test-build-switch test-strict test-benchmark test-release test-warnings test-asan test-lsan
.PHONY: _test _test-harness _test-network _test-chat-smoke _test-output-contract _test-build-switch _test-strict _test-benchmark
.PHONY: _test-warnings _test-release _test-asan _test-lsan
.PHONY: fuzz-build fuzz-corpora fuzz-smoke fuzz-smoke-run
.PHONY: _fuzz-smoke _fuzz-smoke-run
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

# Regenerate the clangd compilation database.
#
# Always starts from a clean tree, deliberately. bear records only the
# compilations it actually observes, so running it over an up-to-date tree
# silently produces a partial database that still looks valid. The build must
# also cover the test targets: `all` alone omits -Itests, which leaves every
# file under tests/ unable to resolve test_assert.h.
compiledb:
	@command -v $(BEAR) >/dev/null 2>&1 || { \
		printf 'bear not found; cannot regenerate %s\n' '$(COMPILEDB)' >&2; \
		exit 1; \
	}
	+$(MAKE) clean
	+$(BEAR) -- $(MAKE) all $(TEST_BINS)

help:
	@printf '%s\n' \
		'Build targets:' \
		'  all              Build scomp, sdiss, sin, and sconv; default BUILD=debug' \
		'  debug            Clean, then build all with BUILD=debug' \
		'  release          Clean, then build all with BUILD=release' \
		'  sanitize         Clean, then build all with BUILD=sanitize and ASan/UBSan' \
		'  lib              Build lib/libsinshared.a only' \
		'  compiledb        Clean, rebuild, and regenerate compile_commands.json via bear' \
		'  clean            Remove objects, binaries, libraries, tests, fuzz artifacts, and stale generated files' \
		'' \
		'Test targets:' \
		'  Successful test targets print concise totals; failures replay captured diagnostics' \
		'  test             Build debug artifacts and run network + combined core/compiler/runtime suite' \
		'  test-framework   Build and run the self-contained C17 framework tests' \
		'  test-conformance  Run fixture-driven source-to-runtime conformance cases' \
		'  inventory-audit  Validate checked-in language, bytecode, API, libcall, executable, and test catalogs' \
		'  test-network     Build and run network tests only' \
		'  test-chat-smoke  Run the real chat example through localhost' \
		'  test-output-contract Verify concise success and diagnostic failure formatting' \
		'  test-build-switch Verify build variants can be switched without cleaning' \
		'  test-strict      Run combined core/compiler/runtime suite with benchmark budgets enabled' \
		'  test-benchmark   Run the opt-in extended benchmark matrix in an optimized build' \
		'  test-release     Clean, rebuild, and test with BUILD=release and strict warnings' \
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
		'  FUZZ_CC=clang                 Compiler used by fuzz targets' \
		'  FUZZ_ARTIFACT_DIR=DIR         Preserve fuzz artifacts in DIR; default is temporary' \
		'  XXD=xxd                       Required tool for sdiss corpus seeding'

$(LIB): $(LIB_OBJECTS)
	@mkdir -p $(LIB_DIR)
	rm -f $@
	ar rcs $@ $^

scomp sdiss sin sconv: %: $(OBJ_DIR)/%.o $(LIB) FORCE_BUILD
	$(CC) -o $@ $(filter-out FORCE_BUILD,$^) $(LDFLAGS) $(LIBS)

FORCE_BUILD:

$(PARSER_GENERATED) &: $(PARSER_SOURCES)
	@mkdir -p $(GENERATED_DIR)
	$(YACC) -o $(PARSER_C) --defines=$(PARSER_H) $<

$(LEXER_GENERATED): $(LEXER_SOURCES) $(PARSER_GENERATED)
	@mkdir -p $(GENERATED_DIR)
	$(LEX) -o $(LEXER_C) $<

$(OBJ_DIR)/parser.o: $(PARSER_C) $(PARSER_H)
	@mkdir -p $(@D)
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(GENERATED_WARNING_FLAGS) $< -o $@
$(OBJ_DIR)/lexer.o: $(LEXER_C) $(PARSER_H)
	@mkdir -p $(@D)
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(GENERATED_WARNING_FLAGS) $< -o $@

# These translation units include parser.h directly or through compiler
# headers. Keep this list narrow so unrelated objects do not rebuild when the
# generated parser header changes.
PARSER_DEPENDENT_OBJECTS := \
	$(OBJ_DIR)/compiler/compiler_context.o \
	$(OBJ_DIR)/compiler/compiler_pipeline.o \
	$(OBJ_DIR)/compiler/emitbc.o \
	$(OBJ_DIR)/libcall/libcall_sys.o \
	$(OBJ_DIR)/runtime/runtime_item_ops.o \
	$(OBJ_DIR)/runtime/runtime_frame.o \
	$(OBJ_DIR)/runtime/interpret.o \
	$(OBJ_DIR)/scomp.o

$(PARSER_DEPENDENT_OBJECTS): $(PARSER_H)

# Include dependency files
-include $(DEPS)

test: inventory-audit $(TEST_BIN) $(NETWORK_TEST_BIN) $(CHAT_SMOKE_BIN) scomp sin sconv
	@$(MAKE) --no-print-directory _test

test-framework: $(FRAMEWORK_BINS)
	@TF_FRAMEWORK_RUNNER="./$(FRAMEWORK_RUNNER_BIN)" TF_FRAMEWORK_NEGATIVE="./$(FRAMEWORK_NEG_BIN)" TEST_JOBS="$${TEST_JOBS:-1}" ./$(FRAMEWORK_RUNNER_BIN) ./$(FRAMEWORK_SELF_BIN) ./$(CONFORMANCE_BIN) $(REWRITE_GROUP1_BINS) $(REWRITE_GROUP2_BINS) $(REWRITE_GROUP3_BINS) $(REWRITE_GROUP4_BINS) $(REWRITE_GROUP5_BINS) $(REWRITE_GROUP6_BINS) $(REWRITE_GROUP7_BINS) $(REWRITE_GROUP8_BINS)
	@TF_FRAMEWORK_RUNNER="./$(FRAMEWORK_RUNNER_BIN)" TF_FRAMEWORK_NEGATIVE="./$(FRAMEWORK_NEG_BIN)" ./$(FRAMEWORK_SELF_BIN) --run runner_discovery_and_jobs
	@tmp_file="$$(mktemp)"; trap 'rm -f "$$tmp_file"' EXIT; \
		if ./$(FRAMEWORK_RUNNER_BIN) ./$(FRAMEWORK_SELF_BIN) ./$(FRAMEWORK_DUP_BIN) >"$$tmp_file" 2>&1; then \
			cat "$$tmp_file"; printf '%s\n' 'duplicate discovery unexpectedly succeeded' >&2; exit 1; \
		fi; grep -F 'TF|ERROR|discovery' "$$tmp_file" >/dev/null

test-conformance: $(CONFORMANCE_BIN) $(FRAMEWORK_RUNNER_BIN) scomp sdiss sin
	@./$(FRAMEWORK_RUNNER_BIN) ./$(CONFORMANCE_BIN)

framework-list: $(FRAMEWORK_BINS)
	@./$(FRAMEWORK_SELF_BIN) --list

inventory-audit: $(LIB)
	@PYTHONDONTWRITEBYTECODE=1 python3 tests/inventory/audit.py --archive "$(LIB)" >/dev/null

inventory-audit-self-test: inventory-audit
	@bash tests/inventory/test_audit.sh "$(LIB)"

_test:
	@$(QUIET_RUNNER) aggregate test \
		"$(QUIET_RUNNER) run test-harness -- ./$(TEST_BIN)" \
		"$(QUIET_RUNNER) run network -- ./$(NETWORK_TEST_BIN)" \
		"$(QUIET_RUNNER) run chat-smoke -- ./$(CHAT_SMOKE_BIN)" \
		"$(QUIET_RUNNER) run quiet-output -- ./$(QUIET_OUTPUT_TEST)"

_test-harness:
	@$(QUIET_RUNNER) run test-harness -- ./$(TEST_BIN)

test-network: $(NETWORK_TEST_BIN)
	@$(QUIET_RUNNER) run network -- ./$(NETWORK_TEST_BIN)

_test-network:
	@$(QUIET_RUNNER) run network -- ./$(NETWORK_TEST_BIN)

test-chat-smoke: $(CHAT_SMOKE_BIN) scomp sin
	@$(QUIET_RUNNER) run chat-smoke -- ./$(CHAT_SMOKE_BIN)

_test-chat-smoke:
	@$(QUIET_RUNNER) run chat-smoke -- ./$(CHAT_SMOKE_BIN)

test-output-contract: $(QUIET_RUNNER) $(QUIET_OUTPUT_TEST)
	@$(QUIET_RUNNER) run quiet-output -- ./$(QUIET_OUTPUT_TEST)

_test-output-contract:
	@$(QUIET_RUNNER) run quiet-output -- ./$(QUIET_OUTPUT_TEST)

test-build-switch:
	@$(MAKE) --no-print-directory _test-build-switch

_test-build-switch:
	+$(MAKE) --no-print-directory BUILD=sanitize all
	+$(MAKE) --no-print-directory BUILD=debug all
	+$(MAKE) --no-print-directory BUILD=release all
	+$(MAKE) --no-print-directory BUILD=debug all
	@$(QUIET_RUNNER) aggregate test-build-switch \
		"$(QUIET_RUNNER) one check-sanitize-interpret -- test -f obj/sanitize-$(notdir $(CC))/runtime/interpret.o" \
		"$(QUIET_RUNNER) one check-release-interpret -- test -f obj/release-$(notdir $(CC))/runtime/interpret.o" \
		"$(QUIET_RUNNER) one check-debug-interpret -- test -f obj/debug-$(notdir $(CC))/runtime/interpret.o"

test-strict: $(TEST_BIN)
	@$(QUIET_RUNNER) run test-strict -- env SIN_STRICT_BENCH=1 ./$(TEST_BIN)

_test-strict:
	@$(QUIET_RUNNER) run test-strict -- env SIN_STRICT_BENCH=1 ./$(TEST_BIN)

test-benchmark:
	+$(MAKE) --no-print-directory BUILD=release _test-benchmark

_test-benchmark: $(TEST_BIN)
	@SIN_EXTENDED_BENCH=1 SIN_BENCH_REPORT=1 ./$(TEST_BIN)

test-warnings:
	+$(MAKE) --no-print-directory STRICT_WARNINGS=1 _test-warnings

_test-warnings: clean
	+$(MAKE) --no-print-directory STRICT_WARNINGS=1 test

test-release:
	+$(MAKE) --no-print-directory _test-release

_test-release: clean
	+$(MAKE) --no-print-directory BUILD=release STRICT_WARNINGS=1 test

test-asan:
	+ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=0" $(MAKE) --no-print-directory _test-asan

_test-asan: clean
	+ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=0" \
		$(MAKE) --no-print-directory BUILD=sanitize STRICT_WARNINGS=1 test

test-lsan:
	+ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=1" $(MAKE) --no-print-directory _test-lsan

_test-lsan: clean
	+ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=1" \
		$(MAKE) --no-print-directory BUILD=sanitize STRICT_WARNINGS=1 test

$(TEST_BIN): $(TEST_SOURCES) $(PARSER_H) $(LIB) scomp sdiss sin sconv FORCE_BUILD
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(TEST_DIR) -o $@ $(TEST_SOURCES) $(LIB) $(LDFLAGS) $(LIBS)

$(NETWORK_TEST_BIN): $(TEST_DIR)/network/test_network.c $(SRC_DIR)/net/network.c $(SRC_DIR)/net/network.h FORCE_BUILD
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(TEST_DIR) \
		-o $@ $(TEST_DIR)/network/test_network.c $(LDFLAGS) $(LIBS)

$(CHAT_SMOKE_BIN): $(TEST_DIR)/network/test_chat_smoke.c scomp sin FORCE_BUILD
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(TEST_DIR) -o $@ $(TEST_DIR)/network/test_chat_smoke.c

$(FRAMEWORK_SELF_BIN): $(FRAMEWORK_SOURCES) $(FRAMEWORK_SELF_SOURCES) $(FRAMEWORK_DIR)/test_framework.h $(SRC_DIR)/config.h $(SRC_DIR)/itemstore/item.h $(SRC_DIR)/itemstore/item_internal.h $(SRC_DIR)/common/memory.h $(LIB)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(FRAMEWORK_DIR) -o $@ $(FRAMEWORK_SOURCES) $(FRAMEWORK_SELF_SOURCES) $(LIB) $(LDFLAGS) $(LIBS)

$(FRAMEWORK_RUNNER_BIN): $(FRAMEWORK_SOURCES) $(FRAMEWORK_RUNNER_SOURCES) $(FRAMEWORK_DIR)/test_framework.h $(SRC_DIR)/config.h $(SRC_DIR)/itemstore/item.h $(SRC_DIR)/itemstore/item_internal.h $(SRC_DIR)/common/memory.h $(LIB)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(FRAMEWORK_DIR) -o $@ $(FRAMEWORK_SOURCES) $(FRAMEWORK_RUNNER_SOURCES) $(LIB) $(LDFLAGS) $(LIBS)

$(FRAMEWORK_DUP_BIN): $(FRAMEWORK_SOURCES) $(FRAMEWORK_DUP_SOURCES) $(FRAMEWORK_DIR)/test_framework.h $(SRC_DIR)/config.h $(SRC_DIR)/itemstore/item.h $(SRC_DIR)/itemstore/item_internal.h $(SRC_DIR)/common/memory.h $(LIB)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(FRAMEWORK_DIR) -o $@ $(FRAMEWORK_SOURCES) $(FRAMEWORK_DUP_SOURCES) $(LIB) $(LDFLAGS) $(LIBS)

$(FRAMEWORK_NEG_BIN): $(FRAMEWORK_SOURCES) $(FRAMEWORK_NEG_SOURCES) $(FRAMEWORK_DIR)/test_framework.h $(SRC_DIR)/config.h $(SRC_DIR)/itemstore/item.h $(SRC_DIR)/itemstore/item_internal.h $(SRC_DIR)/common/memory.h $(LIB)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(FRAMEWORK_DIR) -o $@ $(FRAMEWORK_SOURCES) $(FRAMEWORK_NEG_SOURCES) $(LIB) $(LDFLAGS) $(LIBS)

$(CONFORMANCE_BIN): $(FRAMEWORK_SOURCES) $(CONFORMANCE_DIR)/test_conformance.c $(FRAMEWORK_DIR)/test_framework.h $(CONFORMANCE_FIXTURES) $(TEST_DIR)/fixtures/interpret/list-itemref-persist.src $(TEST_DIR)/inventory/language.csv $(TEST_DIR)/inventory/libcalls.csv $(TEST_DIR)/inventory/contracts.csv $(TEST_DIR)/inventory/tests.csv $(LIB) scomp sdiss sin
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(FRAMEWORK_DIR) -o $@ $(FRAMEWORK_SOURCES) $(CONFORMANCE_DIR)/test_conformance.c $(LIB) $(LDFLAGS) $(LIBS)

REWRITE_GROUP1_CFLAGS = $(TEST_CFLAGS) -DSIN_TEST_FRAMEWORK_COMPAT -I$(TEST_DIR) -I$(FRAMEWORK_DIR)

$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_absyn_lifecycle: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP1_DIR)/adapter_absyn_lifecycle.c $(TEST_DIR)/core/test_absyn_lifecycle.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_absyn_lifecycle.c $(TEST_DIR)/core/test_absyn_lifecycle.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_parser_input_api: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP1_DIR)/adapter_parser_input_api.c $(TEST_DIR)/core/test_parser_input_api.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_parser_input_api.c $(TEST_DIR)/core/test_parser_input_api.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_cli_io: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP1_DIR)/adapter_cli_io.c $(TEST_DIR)/core/test_cli_io.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_cli_io.c $(TEST_DIR)/core/test_cli_io.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_parser_float_literals: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP1_DIR)/adapter_parser_float_literals.c $(TEST_DIR)/core/test_parser_float_literals.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_parser_float_literals.c $(TEST_DIR)/core/test_parser_float_literals.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_sconv: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP1_DIR)/adapter_sconv.c $(TEST_DIR)/core/test_sconv.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_sconv.c $(TEST_DIR)/core/test_sconv.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_value_behavior: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP1_DIR)/adapter_value_behavior.c $(TEST_DIR)/core/test_value_behavior.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_value_behavior.c $(TEST_DIR)/core/test_value_behavior.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_libcall_registry: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP1_DIR)/adapter_libcall_registry.c $(TEST_DIR)/core/test_libcall_registry.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_libcall_registry.c $(TEST_DIR)/core/test_libcall_registry.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_libcall_sys: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP1_DIR)/adapter_libcall_sys.c $(TEST_DIR)/core/test_libcall_sys.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_libcall_sys.c $(TEST_DIR)/core/test_libcall_sys.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_fixture_policy: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP1_DIR)/adapter_fixture_policy.c $(TEST_DIR)/shared/test_fixture_policy.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_fixture_policy.c $(TEST_DIR)/shared/test_fixture_policy.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_output_contract: $(FRAMEWORK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_output_contract.c $(REWRITE_GROUP1_HEADERS) $(LIB)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(FRAMEWORK_DIR) -o $@ $(FRAMEWORK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_output_contract.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP1_DIR)/test_memory: $(FRAMEWORK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_memory.c $(REWRITE_GROUP1_HEADERS) $(LIB)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(FRAMEWORK_DIR) -o $@ $(FRAMEWORK_SOURCES) $(REWRITE_GROUP1_DIR)/adapter_memory.c $(LIB) $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_semant: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group2_adapter_semant.c $(TEST_DIR)/core/test_semant.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group2_adapter_semant.c $(TEST_DIR)/core/test_semant.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_ir_validate: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group2_adapter_ir_validate.c $(TEST_DIR)/core/test_ir_validate.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group2_adapter_ir_validate.c $(TEST_DIR)/core/test_ir_validate.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_relative_item_leading_dot: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group2_adapter_relative_item.c $(TEST_DIR)/core/test_relative_item_leading_dot.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group2_adapter_relative_item.c $(TEST_DIR)/core/test_relative_item_leading_dot.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_pipeline_golden: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group2_adapter_pipeline_golden.c $(TEST_DIR)/compiler/test_pipeline_golden.c scomp sdiss sin sconv
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group2_adapter_pipeline_golden.c $(TEST_DIR)/compiler/test_pipeline_golden.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_pipeline_source_golden: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group2_adapter_pipeline_source_golden.c $(TEST_DIR)/compiler/test_pipeline_source_golden.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group2_adapter_pipeline_source_golden.c $(TEST_DIR)/compiler/test_pipeline_source_golden.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_pipeline_negative_matrix: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group2_adapter_pipeline_negative.c $(TEST_DIR)/compiler/test_pipeline_negative_matrix.c scomp sdiss sin sconv
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group2_adapter_pipeline_negative.c $(TEST_DIR)/compiler/test_pipeline_negative_matrix.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_parser_examples_obj_golden: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group2_adapter_parser_examples.c $(TEST_DIR)/compiler/test_parser_examples_obj_golden.c scomp sdiss sin sconv
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group2_adapter_parser_examples.c $(TEST_DIR)/compiler/test_parser_examples_obj_golden.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_compiler_context_failures: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group2_adapter_compiler_context.c $(TEST_DIR)/compiler/test_compiler_context_failures.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group2_adapter_compiler_context.c $(TEST_DIR)/compiler/test_compiler_context_failures.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_compiler_diag_pipeline: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group2_adapter_compiler_diag.c $(TEST_DIR)/compiler/test_compiler_diag_pipeline.c scomp sdiss sin sconv
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group2_adapter_compiler_diag.c $(TEST_DIR)/compiler/test_compiler_diag_pipeline.c $(LIB) $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_opcode_schema: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_opcode_schema.c $(TEST_DIR)/core/test_opcode_schema.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_opcode_schema.c $(TEST_DIR)/core/test_opcode_schema.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_bytecode_convert: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_bytecode_convert.c $(TEST_DIR)/compiler/test_bytecode_convert.c $(TEST_DIR)/fixtures/bytecode-migration/legacy-0.7.1.hex $(TEST_DIR)/fixtures/bytecode-migration/v1.hex
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_bytecode_convert.c $(TEST_DIR)/compiler/test_bytecode_convert.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_bytecode_v1_abi: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_bytecode_v1_abi.c $(TEST_DIR)/compiler/test_bytecode_v1_abi.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_bytecode_v1_abi.c $(TEST_DIR)/compiler/test_bytecode_v1_abi.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_bytecode_verify: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_bytecode_verify.c $(TEST_DIR)/compiler/test_bytecode_verify.c $(TEST_DIR)/shared/test_pipeline_cases.c $(wildcard $(TEST_DIR)/fixtures/*.hex)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_bytecode_verify.c $(TEST_DIR)/compiler/test_bytecode_verify.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_bytecode_wire: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_bytecode_wire.c $(TEST_DIR)/compiler/test_bytecode_wire.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_bytecode_wire.c $(TEST_DIR)/compiler/test_bytecode_wire.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_all_ir_ops: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_all_ir_ops.c $(TEST_DIR)/compiler/test_emitbc_all_ir_ops_accounted_for.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_all_ir_ops.c $(TEST_DIR)/compiler/test_emitbc_all_ir_ops_accounted_for.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_header: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_header.c $(TEST_DIR)/compiler/test_emitbc_header.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_header.c $(TEST_DIR)/compiler/test_emitbc_header.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_invariants: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_invariants.c $(TEST_DIR)/compiler/test_emitbc_invariants.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_invariants.c $(TEST_DIR)/compiler/test_emitbc_invariants.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_jumps: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_jumps.c $(TEST_DIR)/compiler/test_emitbc_jumps.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_jumps.c $(TEST_DIR)/compiler/test_emitbc_jumps.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_opcode_map: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_opcode_map.c $(TEST_DIR)/compiler/test_emitbc_opcode_map.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_opcode_map.c $(TEST_DIR)/compiler/test_emitbc_opcode_map.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_emitbc_post_verify: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_post_verify.c $(TEST_DIR)/compiler/test_emitbc_post_verify.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_emitbc_post_verify.c $(TEST_DIR)/compiler/test_emitbc_post_verify.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_sdiss_fixtures: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group3_adapter_sdiss_fixtures.c $(TEST_DIR)/compiler/test_sdiss_fixtures.c $(TEST_DIR)/fixtures/sdiss/basic.hex $(TEST_DIR)/fixtures/sdiss/basic.expected.txt sdiss
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group3_adapter_sdiss_fixtures.c $(TEST_DIR)/compiler/test_sdiss_fixtures.c $(LIB) $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_stack_frames: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group4_adapter_stack_frames.c $(TEST_DIR)/core/test_stack_frames.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group4_adapter_stack_frames.c $(TEST_DIR)/core/test_stack_frames.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_list: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group4_adapter_list.c $(TEST_DIR)/core/test_list.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group4_adapter_list.c $(TEST_DIR)/core/test_list.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_interpret_semantics: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group4_adapter_interpret_semantics.c $(TEST_DIR)/interpreter/test_interpret_semantics_golden.c $(TEST_DIR)/fixtures/interpret/*.src $(TEST_DIR)/fixtures/interpret/*.txt $(TEST_DIR)/fixtures/conformance/positive-core.src $(TEST_DIR)/fixtures/conformance/positive-core.runtime.expected.txt scomp sin
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group4_adapter_interpret_semantics.c $(TEST_DIR)/interpreter/test_interpret_semantics_golden.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_interpret_stress: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group4_adapter_interpret_stress.c $(TEST_DIR)/interpreter/test_interpret_stress.c $(TEST_DIR)/fixtures/interpret/*.txt scomp sin
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group4_adapter_interpret_stress.c $(TEST_DIR)/interpreter/test_interpret_stress.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_runtime_benchmark: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group4_adapter_runtime_benchmark.c $(TEST_DIR)/interpreter/test_runtime_benchmark_optin.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group4_adapter_runtime_benchmark.c $(TEST_DIR)/interpreter/test_runtime_benchmark_optin.c $(LIB) $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_item_cache: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group5_adapter_item_cache.c $(TEST_DIR)/core/test_item_cache.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -Wl,--wrap=calloc -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group5_adapter_item_cache.c $(TEST_DIR)/core/test_item_cache.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_itemstore_io: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group5_adapter_itemstore_io.c $(TEST_DIR)/core/test_itemstore_io.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group5_adapter_itemstore_io.c $(TEST_DIR)/core/test_itemstore_io.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_sin_itemstore_policy: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group5_adapter_sin_itemstore_policy.c $(TEST_DIR)/core/test_sin_itemstore_policy.c scomp sin
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group5_adapter_sin_itemstore_policy.c $(TEST_DIR)/core/test_sin_itemstore_policy.c $(LIB) $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_libcall_task: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group6_adapter_libcall_task.c $(TEST_DIR)/core/test_libcall_task.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group6_adapter_libcall_task.c $(TEST_DIR)/core/test_libcall_task.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_task_lifecycle: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group6_adapter_task_lifecycle.c $(TEST_DIR)/core/test_task_lifecycle.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group6_adapter_task_lifecycle.c $(TEST_DIR)/core/test_task_lifecycle.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_libcall_net: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group6_adapter_libcall_net.c $(TEST_DIR)/core/test_libcall_net.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group6_adapter_libcall_net.c $(TEST_DIR)/core/test_libcall_net.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_libcall_str: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group6_adapter_libcall_str.c $(TEST_DIR)/core/test_libcall_str.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group6_adapter_libcall_str.c $(TEST_DIR)/core/test_libcall_str.c $(LIB) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_libcall_list: $(REWRITE_GROUP1_COMMON_DEPS) $(REWRITE_GROUP2_DIR)/group6_adapter_libcall_list.c $(TEST_DIR)/core/test_libcall_list.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP1_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group6_adapter_libcall_list.c $(TEST_DIR)/core/test_libcall_list.c $(LIB) $(LDFLAGS) $(LIBS)
REWRITE_GROUP6_SYSCOMP_LINK_SOURCES := $(FRAMEWORK_DIR)/test_framework.c $(TEST_DIR)/shared/test_helpers.c
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_libcall_sys_compile: $(REWRITE_GROUP6_SYSCOMP_LINK_SOURCES) $(REWRITE_GROUP1_HEADERS) $(REWRITE_GROUP2_DIR)/group6_adapter_libcall_sys_compile.c $(TEST_DIR)/core/test_libcall_sys_compile.c $(LIB)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(REWRITE_GROUP6_SYSCOMP_LINK_SOURCES) $(REWRITE_GROUP2_DIR)/group6_adapter_libcall_sys_compile.c $(TEST_DIR)/core/test_libcall_sys_compile.c $(LIB) $(LDFLAGS) $(LIBS)

# Group 7 network is a white-box translation unit: its adapter directly
# includes the legacy source, which owns CONFIG_t, stubs, and implementation
# inclusions.  Do not add framework_config.c or normal network objects here.
$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_network: $(FRAMEWORK_DIR)/test_framework.c $(REWRITE_GROUP2_DIR)/group7_adapter_network.c $(TEST_DIR)/network/test_network.c $(SRC_DIR)/net/network.c $(SRC_DIR)/net/libtelnet.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(REWRITE_GROUP1_CFLAGS) -o $@ $(FRAMEWORK_DIR)/test_framework.c $(REWRITE_GROUP2_DIR)/group7_adapter_network.c $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_chat_smoke: $(FRAMEWORK_SOURCES) $(REWRITE_GROUP2_DIR)/group7_adapter_chat_smoke.c $(TEST_DIR)/network/test_chat_smoke.c $(LIB) scomp sin
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(TEST_DIR) -I$(FRAMEWORK_DIR) -o $@ $(FRAMEWORK_SOURCES) $(REWRITE_GROUP2_DIR)/group7_adapter_chat_smoke.c $(LIB) $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/$(REWRITE_GROUP2_DIR)/test_cli_contract_matrix: $(FRAMEWORK_SOURCES) $(REWRITE_GROUP2_DIR)/group8_adapter_cli_contract_matrix.c $(FRAMEWORK_DIR)/test_framework.h $(LIB) scomp sdiss sin sconv
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -I$(FRAMEWORK_DIR) -o $@ $(FRAMEWORK_SOURCES) $(REWRITE_GROUP2_DIR)/group8_adapter_cli_contract_matrix.c $(LIB) $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/tests/fuzz/%.o : $(FUZZ_DIR)/%.c $(PARSER_GENERATED)
	@mkdir -p $(@D)
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

$(FUZZ_BIN): $(OBJ_DIR)/tests/fuzz/fuzz_scomp.o $(LIB)
	$(CC) -o $@ $^ $(FUZZ_LINK_FLAGS) $(LIBS)

$(FUZZ_SDISS_BIN): $(OBJ_DIR)/tests/fuzz/fuzz_sdiss.o $(LIB)
	$(CC) -o $@ $^ $(FUZZ_LINK_FLAGS) $(LIBS)

$(FUZZ_SIN_OBJECT_BIN): $(OBJ_DIR)/tests/fuzz/fuzz_sin_object.o $(LIB)
	$(CC) -o $@ $^ $(FUZZ_LINK_FLAGS) $(LIBS)

seed-fuzz-sdiss-corpus:
	@set -eu; \
	command -v "$(XXD)" >/dev/null 2>&1 || { \
		printf 'Required fuzz corpus tool not found: %s\n' "$(XXD)" >&2; \
		exit 1; \
	}; \
	mkdir -p $(FUZZ_SDISS_CORPUS_DIR); \
	for hex in $(TEST_DIR)/fixtures/sdiss/*.hex; do \
		[ -e "$$hex" ] || continue; \
		"$(XXD)" -r -p "$$hex" "$(FUZZ_SDISS_CORPUS_DIR)/$$(basename "$$hex" .hex).obj" || { \
			printf 'Failed to seed fuzz corpus from %s\n' "$$hex" >&2; \
			exit 1; \
		}; \
	done

fuzz-corpora: seed-fuzz-sdiss-corpus seed-fuzz-sin-object-corpus

fuzz-build: clean
	+$(FUZZ_MAKE) fuzz-corpora $(FUZZ_BINS)

fuzz-smoke:
	+$(MAKE) --no-print-directory _fuzz-smoke

_fuzz-smoke: clean
	+$(FUZZ_MAKE) --no-print-directory fuzz-corpora $(FUZZ_BINS)
	@$(QUIET_RUNNER) run fuzz-smoke -- $(MAKE) --no-print-directory _fuzz-smoke-run

# Runs the already-built fuzz harnesses against seeded corpora.
fuzz-smoke-run:
	@$(QUIET_RUNNER) run fuzz-smoke-run -- $(MAKE) --no-print-directory _fuzz-smoke-run

_fuzz-smoke-run:
	@set -eu; \
	for bin in $(FUZZ_BINS); do \
		[ -x "$$bin" ] || { printf 'Missing %s; run make fuzz-build first.\n' "$$bin" >&2; exit 1; }; \
	done; \
	work_dir="$$(mktemp -d)"; \
	trap 'rm -rf "$$work_dir"' EXIT; \
	artifact_dir="$(FUZZ_ARTIFACT_DIR)"; \
	if [ -z "$$artifact_dir" ]; then \
		artifact_dir="$$work_dir/artifacts"; \
	fi; \
	mkdir -p "$$work_dir/scomp" "$$work_dir/sdiss" "$$work_dir/sin-object" \
		"$$artifact_dir/scomp" "$$artifact_dir/sdiss" "$$artifact_dir/sin-object"; \
	failed=0; total=0; for spec in "scomp $(FUZZ_BIN) $(FUZZ_CORPUS_DIR)" "sdiss $(FUZZ_SDISS_BIN) $(FUZZ_SDISS_CORPUS_DIR)" "sin-object $(FUZZ_SIN_OBJECT_BIN) $(FUZZ_SIN_OBJECT_CORPUS_DIR)"; do set -- $$spec; name=$$1; bin=$$2; corpus=$$3; log="$$work_dir/$$name.log"; total=$$((total+1)); if ASAN_OPTIONS="$${ASAN_OPTIONS:-}:detect_leaks=0" "$$bin" -runs=$(FUZZ_RUNS) -max_total_time=$(FUZZ_TIME) -seed=$(FUZZ_SEED) -artifact_prefix="$$artifact_dir/$$name/" "$$work_dir/$$name" "$$corpus" >"$$log" 2>&1; then printf '[fuzz:%s] ran=1 passed=1 failed=0 status=SUCCESS\n' "$$name"; else cat "$$log"; printf '[fuzz:%s] ran=1 passed=0 failed=1 status=FAILURE\n' "$$name"; failed=$$((failed+1)); fi; done; if [ $$failed -ne 0 ]; then printf '[fuzz] totals: ran=%s passed=%s failed=%s skipped=0 status=FAILURE\n' "$$total" "$$((total-failed))" "$$failed"; exit 1; fi; printf '[fuzz] totals: ran=%s passed=%s failed=0 skipped=0 status=SUCCESS\n' "$$total" "$$total"

fuzz-scomp: clean
	+$(FUZZ_MAKE) $(FUZZ_BIN)
	@printf 'Built %s. Run with: %s %s\n' "$(FUZZ_BIN)" "$(FUZZ_BIN)" "$(FUZZ_CORPUS_DIR)"

fuzz-sdiss: clean
	+$(FUZZ_MAKE) seed-fuzz-sdiss-corpus $(FUZZ_SDISS_BIN)
	@printf 'Built %s. Run with: %s %s\n' "$(FUZZ_SDISS_BIN)" "$(FUZZ_SDISS_BIN)" "$(FUZZ_SDISS_CORPUS_DIR)"

seed-fuzz-sin-object-corpus: scomp
	@set -eu; \
	command -v "$(XXD)" >/dev/null 2>&1 || { \
		printf 'Required fuzz corpus tool not found: %s\n' "$(XXD)" >&2; \
		exit 1; \
	}; \
	mkdir -p $(FUZZ_SIN_OBJECT_CORPUS_DIR); \
	for src in examples/chat-boot.src examples/chat-load.src examples/echo-boot.src examples/echo-load.src; do \
		[ -e "$$src" ] || continue; \
		obj="$(FUZZ_SIN_OBJECT_CORPUS_DIR)/$$(basename "$$src" .src).obj"; \
		./scomp "$$src" "$$obj" >/dev/null 2>&1 || rm -f "$$obj"; \
	done; \
	for hex in $(TEST_DIR)/fixtures/itemstore/v1-valid.hex \
		$(TEST_DIR)/fixtures/itemstore/v2-nested-ref-valid.hex \
		$(TEST_DIR)/fixtures/itemstore/v2-nested-recursive-malformed.hex; do \
		name="$$(basename "$$hex" .hex)"; \
		sed '/^[[:space:]]*#/d' "$$hex" | "$(XXD)" -r -p \
			> "$(FUZZ_SIN_OBJECT_CORPUS_DIR)/$$name.itemstore" || { \
			printf 'Failed to seed fuzz corpus from %s\n' "$$hex" >&2; exit 1; }; \
	done

# Builds a libFuzzer harness for sin object/itemstore loading and strict bytecode validation.
fuzz-sin-object: clean
	+$(FUZZ_MAKE) seed-fuzz-sin-object-corpus $(FUZZ_SIN_OBJECT_BIN)
	@printf 'Built %s. Run with: %s %s\n' "$(FUZZ_SIN_OBJECT_BIN)" "$(FUZZ_SIN_OBJECT_BIN)" "$(FUZZ_SIN_OBJECT_CORPUS_DIR)"

clean:
	rm -rf obj lib $(PROGRAMS) $(TEST_BINS) $(TEST_DEPS) \
		$(TEST_TMP_ARTIFACTS) $(FUZZ_BINS) $(GENERATED_FUZZ_CORPUS) $(FUZZ_LOCAL_ARTIFACT_DIR) \
		$(SRC_DIR)/parser.c $(SRC_DIR)/parser.h $(SRC_DIR)/lexer.c
	find $(TEST_DIR)/fixtures -type f \( -name '*.tmp' -o -name '*.tmp.*' \
		-o -name '*.generated.obj' -o -name '*.reference.obj' \) -delete
