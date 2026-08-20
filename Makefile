# Sinistra build and test entry point.  Reusable implementation lives in mk/.
ifeq ($(origin CC),default)
CC := gcc
endif
PKG_CONFIG ?= pkg-config
LIBUV_PC ?= libuv
BUILD ?= debug
CSTD ?= c17
SRC_DIR := src
TEST_DIR := tests
CC_VERSION := $(shell $(CC) --version 2>/dev/null | tr '[:upper:]' '[:lower:]' | tr '\n' ' ')
CC_VENDOR := $(if $(findstring clang,$(CC_VERSION)),clang,$(if $(or $(findstring gcc,$(CC_VERSION)),$(findstring free software foundation,$(CC_VERSION))),gcc,unsupported))
CC_MAJOR := $(shell printf '%s\n' '$(CC_VERSION)' | sed -n -E 's/^[^0-9]*([0-9]+)(\.[0-9]+)+.*/\1/p' | head -n 1)
BUILD_TAG := $(BUILD)-$(notdir $(CC))
OBJ_DIR := obj/$(BUILD_TAG)
LIB_DIR := lib/$(BUILD_TAG)
GENERATED_DIR := $(OBJ_DIR)/generated
ifeq ($(origin YACC),default)
YACC := bison
endif
ifeq ($(origin LEX),default)
LEX := flex
endif

BASE_CFLAGS := -std=$(CSTD) -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -MMD -MP
CPPFLAGS := -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -I$(SRC_DIR) -I$(SRC_DIR)/common -I$(SRC_DIR)/bytecode -I$(SRC_DIR)/runtime -I$(SRC_DIR)/itemstore -I$(SRC_DIR)/libcall -I$(SRC_DIR)/net -I$(GENERATED_DIR)
LIBUV_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(LIBUV_PC) 2>/dev/null)
LIBUV_LIBS := $(shell $(PKG_CONFIG) --libs $(LIBUV_PC) 2>/dev/null || printf '%s' '-luv')
LIBS ?= $(LIBUV_LIBS) -lm
CFLAGS ?= $(BASE_CFLAGS)
LDFLAGS ?=
ASAN_OPTIONS ?= strict_string_checks=1:abort_on_error=1
UBSAN_OPTIONS ?= print_stacktrace=1:halt_on_error=1
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
STRICT_WARNING_FLAGS := -Werror -Wshadow -Wformat=2
GENERATED_WARNING_FLAGS := -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=pedantic \
	-Wno-error=unused-but-set-variable -Wno-error=format -Wno-error=format-nonliteral

ifeq ($(BUILD),debug)
CFLAGS += -g -O0 -DDEBUG=1
LDFLAGS += -g
else ifeq ($(BUILD),release)
CFLAGS += -O2 -DNDEBUG
else ifeq ($(BUILD),sanitize)
CFLAGS += -g -O1 -DDEBUG=1 $(SANITIZE_FLAGS)
LDFLAGS += -g $(SANITIZE_FLAGS)
else ifeq ($(BUILD),coverage)
CFLAGS += -g -O0
ifeq ($(CC_VENDOR),clang)
ifneq ($(CC_MAJOR),18)
$(error BUILD=coverage requires reviewed Clang 18 tools; found $(CC_MAJOR))
endif
CFLAGS += -fprofile-instr-generate -fcoverage-mapping -DSIN_COVERAGE_CLANG=1
LDFLAGS += -fprofile-instr-generate -fcoverage-mapping
else ifeq ($(CC_VENDOR),gcc)
ifneq ($(CC_MAJOR),13)
$(error BUILD=coverage requires reviewed GCC 13 tools; found $(CC_MAJOR))
endif
CFLAGS += -fprofile-arcs -ftest-coverage -DSIN_COVERAGE_GCC=1
LDFLAGS += -fprofile-arcs -ftest-coverage
else
$(error BUILD=coverage requires GCC or Clang)
endif
else
$(error Unknown BUILD '$(BUILD)'; expected debug, release, sanitize, or coverage)
endif
CFLAGS += $(LIBUV_CFLAGS)
CFLAGS += $(STRICT_WARNING_FLAGS)

ifeq ($(CC_VENDOR),clang)
LLVM_COV ?= llvm-cov-$(CC_MAJOR)
LLVM_PROFDATA ?= llvm-profdata-$(CC_MAJOR)
GCOV ?= gcov
GCOV_TOOL ?= gcov-tool
else
LLVM_COV ?= llvm-cov
LLVM_PROFDATA ?= llvm-profdata
GCOV ?= gcov-$(CC_MAJOR)
GCOV_TOOL ?= gcov-tool-$(CC_MAJOR)
endif

LIB := $(LIB_DIR)/libsinshared.a
PROGRAMS := scomp sdiss sin sconv
include mk/build.mk
include mk/tests.mk
include mk/fuzz.mk

.DEFAULT_GOAL := all
.PHONY: all lib clean help compiledb bench test test-sanitize test-fuzz test-full $(PROGRAMS)

all: $(PROGRAMS)
lib: $(LIB)

help:
	@printf '%s\n' \
		'Build targets:' \
		'  all              Build scomp, sdiss, sin, and sconv (default BUILD=debug)' \
		'  lib              Build the shared library for the active BUILD/CC variant' \
		'  compiledb        Regenerate compile_commands.json with bear' \
		'  clean            Remove all build outputs, generated data, and test artifacts' \
		'  bench            Run opt-in performance measurements in BUILD=release' \
		'' \
		'Test targets:' \
		'  test             Strict deterministic unit, contract, conformance, CLI, network, and integration suite' \
		'  test-sanitize    Deterministic suite under ASan, UBSan, and leak detection (requires no ptrace restriction)' \
		'  test-fuzz        Seed, build, and run compiler, disassembler, and runtime/itemstore fuzz smoke campaigns' \
		'  test-full        Debug/release, coverage, sanitizer, and fuzz gates' \
		'' \
		'Variables: BUILD=debug|release|sanitize|coverage CC=gcc|clang TEST_JOBS=N' \
		'FUZZ_RUNS=N FUZZ_TIME=seconds FUZZ_SEED=N FUZZ_ARTIFACT_DIR=DIR'

bench:
	+$(MAKE) --no-print-directory BUILD=release _bench

compiledb:
	@command -v bear >/dev/null 2>&1 || { printf '%s\n' 'bear is required for compiledb' >&2; exit 1; }
	+$(MAKE) --no-print-directory clean
	+bear -- $(MAKE) --no-print-directory all test

clean:
	rm -rf obj lib $(PROGRAMS) compile_commands.json
	find $(SRC_DIR) -maxdepth 1 -type f \( -name 'parser.c' -o -name 'parser.h' -o -name 'lexer.c' \) -delete
