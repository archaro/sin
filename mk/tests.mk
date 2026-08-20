# Migrated C17 framework binaries and deterministic test recipes.
FRAMEWORK_DIR := $(TEST_DIR)/framework
CONFORMANCE_DIR := $(TEST_DIR)/conformance
REWRITE_DIR := $(TEST_DIR)/rewrite
FRAMEWORK_SOURCES := $(FRAMEWORK_DIR)/test_framework.c $(FRAMEWORK_DIR)/framework_config.c
FRAMEWORK_HEADERS := $(FRAMEWORK_DIR)/test_framework.h $(SRC_DIR)/config.h \
 $(SRC_DIR)/itemstore/item.h $(SRC_DIR)/itemstore/item_internal.h $(SRC_DIR)/common/memory.h
FRAMEWORK_SELF_BIN := $(OBJ_DIR)/$(FRAMEWORK_DIR)/framework-selftest
FRAMEWORK_RUNNER_BIN := $(OBJ_DIR)/$(FRAMEWORK_DIR)/framework-runner
FRAMEWORK_DUP_BIN := $(OBJ_DIR)/$(FRAMEWORK_DIR)/framework-duplicate-fixture
FRAMEWORK_NEG_BIN := $(OBJ_DIR)/$(FRAMEWORK_DIR)/framework-negative-fixture
CONFORMANCE_BIN := $(OBJ_DIR)/$(CONFORMANCE_DIR)/test-conformance
TEST_OBJECT_DIR := $(OBJ_DIR)/$(TEST_DIR)/objects
test_object = $(TEST_OBJECT_DIR)/$(1:.c=.o)
TEST_SOURCES := $(wildcard $(FRAMEWORK_DIR)/*.c $(CONFORMANCE_DIR)/*.c \
 $(TEST_DIR)/shared/*.c $(TEST_DIR)/core/*.c $(TEST_DIR)/compiler/*.c \
 $(TEST_DIR)/interpreter/*.c $(TEST_DIR)/rewrite/*.c \
 $(TEST_DIR)/rewrite/group1/*.c $(TEST_DIR)/network/*.c)
TEST_OBJECTS := $(foreach source,$(TEST_SOURCES),$(call test_object,$(source)))
REWRITE_COMMON_SOURCES := $(FRAMEWORK_SOURCES) $(TEST_DIR)/shared/test_helpers.c \
 $(TEST_DIR)/shared/test_libcall_support.c $(TEST_DIR)/shared/test_pipeline_cases.c
REWRITE_COMMON := $(foreach source,$(REWRITE_COMMON_SOURCES),$(call test_object,$(source)))
TEST_PROGRAMS := $(VARIANT_PROGRAMS)

REWRITE_GROUP1_BINS := $(addprefix $(OBJ_DIR)/$(REWRITE_DIR)/group1/test_,absyn_lifecycle parser_input_api cli_io parser_float_literals sconv value_behavior libcall_registry libcall_sys fixture_policy output_contract memory)
REWRITE_GROUP2_BINS := $(addprefix $(OBJ_DIR)/$(REWRITE_DIR)/,test_semant test_ir_validate test_relative_item_leading_dot test_pipeline_golden test_pipeline_source_golden test_pipeline_negative_matrix test_parser_examples_obj_golden test_compiler_context_failures test_compiler_diag_pipeline test_opcode_schema test_bytecode_convert test_bytecode_v1_abi test_bytecode_verify test_bytecode_wire test_emitbc_all_ir_ops test_emitbc_header test_emitbc_invariants test_emitbc_jumps test_emitbc_opcode_map test_emitbc_post_verify test_sdiss_fixtures test_stack_frames test_list test_interpret_semantics test_interpret_stress test_runtime_benchmark test_item_cache test_itemstore_io test_sin_itemstore_policy test_libcall_task test_task_lifecycle test_libcall_net test_libcall_str test_libcall_list test_libcall_sys_compile test_network test_chat_smoke test_cli_contract_matrix)
REWRITE_BINS := $(REWRITE_GROUP1_BINS) $(REWRITE_GROUP2_BINS)
TEST_BINS := $(FRAMEWORK_SELF_BIN) $(FRAMEWORK_RUNNER_BIN) $(FRAMEWORK_DUP_BIN) $(FRAMEWORK_NEG_BIN) $(CONFORMANCE_BIN) $(REWRITE_BINS)
TEST_CFLAGS := $(CFLAGS) $(STRICT_WARNING_FLAGS)
TEST_CPPFLAGS := $(CPPFLAGS) -I$(TEST_DIR) -I$(FRAMEWORK_DIR)
CONFORMANCE_FIXTURES := $(TEST_DIR)/fixtures/conformance/conformance.manifest $(wildcard $(TEST_DIR)/fixtures/conformance/*.src $(TEST_DIR)/fixtures/conformance/*.txt $(TEST_DIR)/fixtures/conformance/negative/*.src $(TEST_DIR)/fixtures/conformance/negative/*.txt)

# Records are target suffix | adapter | native test body.  The native bodies
# remain in their original subsystem directories while adapters own metadata.
REWRITE_COMMON_CASES := \
 group1/test_absyn_lifecycle|group1/adapter_absyn_lifecycle.c|core/test_absyn_lifecycle.c \
 group1/test_parser_input_api|group1/adapter_parser_input_api.c|core/test_parser_input_api.c \
 group1/test_cli_io|group1/adapter_cli_io.c|core/test_cli_io.c \
 group1/test_parser_float_literals|group1/adapter_parser_float_literals.c|core/test_parser_float_literals.c \
 group1/test_sconv|group1/adapter_sconv.c|core/test_sconv.c \
 group1/test_value_behavior|group1/adapter_value_behavior.c|core/test_value_behavior.c \
 group1/test_libcall_registry|group1/adapter_libcall_registry.c|core/test_libcall_registry.c \
 group1/test_libcall_sys|group1/adapter_libcall_sys.c|core/test_libcall_sys.c \
 test_semant|group2_adapter_semant.c|core/test_semant.c \
 test_ir_validate|group2_adapter_ir_validate.c|core/test_ir_validate.c \
 test_relative_item_leading_dot|group2_adapter_relative_item.c|core/test_relative_item_leading_dot.c \
 test_pipeline_golden|group2_adapter_pipeline_golden.c|compiler/test_pipeline_golden.c \
 test_pipeline_source_golden|group2_adapter_pipeline_source_golden.c|compiler/test_pipeline_source_golden.c \
 test_pipeline_negative_matrix|group2_adapter_pipeline_negative.c|compiler/test_pipeline_negative_matrix.c \
 test_parser_examples_obj_golden|group2_adapter_parser_examples.c|compiler/test_parser_examples_obj_golden.c \
 test_compiler_context_failures|group2_adapter_compiler_context.c|compiler/test_compiler_context_failures.c \
 test_compiler_diag_pipeline|group2_adapter_compiler_diag.c|compiler/test_compiler_diag_pipeline.c \
 test_opcode_schema|group3_adapter_opcode_schema.c|core/test_opcode_schema.c \
 test_bytecode_convert|group3_adapter_bytecode_convert.c|compiler/test_bytecode_convert.c \
 test_bytecode_v1_abi|group3_adapter_bytecode_v1_abi.c|compiler/test_bytecode_v1_abi.c \
 test_bytecode_verify|group3_adapter_bytecode_verify.c|compiler/test_bytecode_verify.c \
 test_bytecode_wire|group3_adapter_bytecode_wire.c|compiler/test_bytecode_wire.c \
 test_emitbc_all_ir_ops|group3_adapter_emitbc_all_ir_ops.c|compiler/test_emitbc_all_ir_ops_accounted_for.c \
 test_emitbc_header|group3_adapter_emitbc_header.c|compiler/test_emitbc_header.c \
 test_emitbc_invariants|group3_adapter_emitbc_invariants.c|compiler/test_emitbc_invariants.c \
 test_emitbc_jumps|group3_adapter_emitbc_jumps.c|compiler/test_emitbc_jumps.c \
 test_emitbc_opcode_map|group3_adapter_emitbc_opcode_map.c|compiler/test_emitbc_opcode_map.c \
 test_emitbc_post_verify|group3_adapter_emitbc_post_verify.c|compiler/test_emitbc_post_verify.c \
 test_sdiss_fixtures|group3_adapter_sdiss_fixtures.c|compiler/test_sdiss_fixtures.c \
 test_stack_frames|group4_adapter_stack_frames.c|core/test_stack_frames.c \
 test_list|group4_adapter_list.c|core/test_list.c \
 test_interpret_semantics|group4_adapter_interpret_semantics.c|interpreter/test_interpret_semantics_golden.c \
 test_interpret_stress|group4_adapter_interpret_stress.c|interpreter/test_interpret_stress.c \
 test_runtime_benchmark|group4_adapter_runtime_benchmark.c|interpreter/test_runtime_benchmark_optin.c \
 test_item_cache|group5_adapter_item_cache.c|core/test_item_cache.c \
 test_itemstore_io|group5_adapter_itemstore_io.c|core/test_itemstore_io.c \
 test_sin_itemstore_policy|group5_adapter_sin_itemstore_policy.c|core/test_sin_itemstore_policy.c \
 test_libcall_task|group6_adapter_libcall_task.c|core/test_libcall_task.c \
 test_task_lifecycle|group6_adapter_task_lifecycle.c|core/test_task_lifecycle.c \
 test_libcall_net|group6_adapter_libcall_net.c|core/test_libcall_net.c \
 test_libcall_str|group6_adapter_libcall_str.c|core/test_libcall_str.c \
 test_libcall_list|group6_adapter_libcall_list.c|core/test_libcall_list.c

define rewrite_common_template
$(OBJ_DIR)/$(REWRITE_DIR)/$(1): $(REWRITE_COMMON) $(call test_object,$(REWRITE_DIR)/$(2)) $(call test_object,$(TEST_DIR)/$(3)) $(LIB) $(TEST_PROGRAMS)
	@mkdir -p $$(@D)
	$(CC) -o $$@ $(REWRITE_COMMON) $(call test_object,$(REWRITE_DIR)/$(2)) $(call test_object,$(TEST_DIR)/$(3)) $(LIB) $$(LDFLAGS) $$(LIBS)
endef
$(foreach c,$(REWRITE_COMMON_CASES),$(eval $(call rewrite_common_template,$(word 1,$(subst |, ,$(c))),$(word 2,$(subst |, ,$(c))),$(word 3,$(subst |, ,$(c))))))

$(OBJ_DIR)/$(REWRITE_DIR)/test_item_cache: LDFLAGS += -Wl,--wrap=calloc

REWRITE_FRAMEWORK_ONLY_CASES := \
 group1/test_output_contract|group1/adapter_output_contract.c \
 group1/test_memory|group1/adapter_memory.c \
 test_cli_contract_matrix|group8_adapter_cli_contract_matrix.c
define rewrite_framework_only_template
$(OBJ_DIR)/$(REWRITE_DIR)/$(1): $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(FRAMEWORK_HEADERS) $(call test_object,$(TEST_DIR)/shared/test_helpers.c) $(call test_object,$(REWRITE_DIR)/$(2)) $(LIB) $(TEST_PROGRAMS)
	@mkdir -p $$(@D)
	$(CC) -o $$@ $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(TEST_DIR)/shared/test_helpers.c) $(call test_object,$(REWRITE_DIR)/$(2)) $(LIB) $$(LDFLAGS) $$(LIBS)
endef
$(foreach c,$(REWRITE_FRAMEWORK_ONLY_CASES),$(eval $(call rewrite_framework_only_template,$(word 1,$(subst |, ,$(c))),$(word 2,$(subst |, ,$(c))))))

$(OBJ_DIR)/$(REWRITE_DIR)/group1/test_fixture_policy: $(REWRITE_COMMON) $(call test_object,$(REWRITE_DIR)/group1/adapter_fixture_policy.c) $(call test_object,$(TEST_DIR)/shared/test_fixture_policy.c) $(LIB) $(TEST_PROGRAMS)
	@mkdir -p $(@D)
	$(CC) -o $@ $(REWRITE_COMMON) $(call test_object,$(REWRITE_DIR)/group1/adapter_fixture_policy.c) $(call test_object,$(TEST_DIR)/shared/test_fixture_policy.c) $(LIB) $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/$(REWRITE_DIR)/test_libcall_sys_compile: $(call test_object,$(FRAMEWORK_DIR)/test_framework.c) $(call test_object,$(TEST_DIR)/shared/test_helpers.c) $(call test_object,$(REWRITE_DIR)/group6_adapter_libcall_sys_compile.c) $(call test_object,$(TEST_DIR)/core/test_libcall_sys_compile.c) $(LIB) $(TEST_PROGRAMS)
	@mkdir -p $(@D)
	$(CC) -o $@ $(call test_object,$(FRAMEWORK_DIR)/test_framework.c) $(call test_object,$(TEST_DIR)/shared/test_helpers.c) $(call test_object,$(REWRITE_DIR)/group6_adapter_libcall_sys_compile.c) $(call test_object,$(TEST_DIR)/core/test_libcall_sys_compile.c) $(LIB) $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/$(REWRITE_DIR)/test_network: $(call test_object,$(FRAMEWORK_DIR)/test_framework.c) $(call test_object,$(REWRITE_DIR)/group7_adapter_network.c)
	@mkdir -p $(@D)
	$(CC) -o $@ $(call test_object,$(FRAMEWORK_DIR)/test_framework.c) $(call test_object,$(REWRITE_DIR)/group7_adapter_network.c) $(LDFLAGS) $(LIBS)
$(OBJ_DIR)/$(REWRITE_DIR)/test_chat_smoke: $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(TEST_DIR)/shared/test_helpers.c) $(call test_object,$(REWRITE_DIR)/group7_adapter_chat_smoke.c) $(LIB) $(TEST_PROGRAMS)
	@mkdir -p $(@D)
	$(CC) -o $@ $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(TEST_DIR)/shared/test_helpers.c) $(call test_object,$(REWRITE_DIR)/group7_adapter_chat_smoke.c) $(LIB) $(LDFLAGS) $(LIBS)

$(FRAMEWORK_SELF_BIN): $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(FRAMEWORK_DIR)/framework_selftest.c) $(FRAMEWORK_HEADERS) $(LIB)
	@mkdir -p $(@D)
	$(CC) -o $@ $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(FRAMEWORK_DIR)/framework_selftest.c) $(LIB) $(LDFLAGS) $(LIBS)
$(FRAMEWORK_RUNNER_BIN): $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(FRAMEWORK_DIR)/test_runner.c) $(FRAMEWORK_HEADERS) $(LIB)
	@mkdir -p $(@D)
	$(CC) -o $@ $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(FRAMEWORK_DIR)/test_runner.c) $(LIB) $(LDFLAGS) $(LIBS)
$(FRAMEWORK_DUP_BIN): $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(FRAMEWORK_DIR)/framework_duplicate_fixture.c) $(FRAMEWORK_HEADERS) $(LIB)
	@mkdir -p $(@D)
	$(CC) -o $@ $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(FRAMEWORK_DIR)/framework_duplicate_fixture.c) $(LIB) $(LDFLAGS) $(LIBS)
$(FRAMEWORK_NEG_BIN): $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(FRAMEWORK_DIR)/framework_negative_fixture.c) $(FRAMEWORK_HEADERS) $(LIB)
	@mkdir -p $(@D)
	$(CC) -o $@ $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(FRAMEWORK_DIR)/framework_negative_fixture.c) $(LIB) $(LDFLAGS) $(LIBS)
$(CONFORMANCE_BIN): $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(TEST_DIR)/shared/test_helpers.c) $(call test_object,$(CONFORMANCE_DIR)/test_conformance.c) $(FRAMEWORK_HEADERS) $(CONFORMANCE_FIXTURES) $(LIB) $(TEST_PROGRAMS)
	@mkdir -p $(@D)
	$(CC) -o $@ $(foreach source,$(FRAMEWORK_SOURCES),$(call test_object,$(source))) $(call test_object,$(TEST_DIR)/shared/test_helpers.c) $(call test_object,$(CONFORMANCE_DIR)/test_conformance.c) $(LIB) $(LDFLAGS) $(LIBS)

TEST_PROGRAM_ENV = SIN_TEST_SCOMP="$(abspath $(VARIANT_BIN_DIR)/scomp)" SIN_TEST_SDISS="$(abspath $(VARIANT_BIN_DIR)/sdiss)" SIN_TEST_SIN="$(abspath $(VARIANT_BIN_DIR)/sin)" SIN_TEST_SCONV="$(abspath $(VARIANT_BIN_DIR)/sconv)"
TEST_RUNNER_ENV = $(TEST_PROGRAM_ENV) TF_FRAMEWORK_RUNNER="./$(FRAMEWORK_RUNNER_BIN)" TF_FRAMEWORK_NEGATIVE="./$(FRAMEWORK_NEG_BIN)" TF_FRAMEWORK_SELF="./$(FRAMEWORK_SELF_BIN)" TF_FRAMEWORK_DUPLICATE="./$(FRAMEWORK_DUP_BIN)" TF_FRAMEWORK_CONFORMANCE="./$(CONFORMANCE_BIN)" TEST_JOBS="$${TEST_JOBS:-1}"
TEST_RUN_LIST := $(FRAMEWORK_SELF_BIN) $(CONFORMANCE_BIN) $(REWRITE_BINS)
TEST_TMP_ROOT := $(abspath $(OBJ_DIR)/tmp)
COVERAGE_OBJ_DIR := obj/coverage-$(notdir $(CC))
COVERAGE_LIB_DIR := lib/coverage-$(notdir $(CC))
ifeq ($(CC_VENDOR),gcc)
COVERAGE_RUN_ENV := GCOV_PREFIX_BASE="$(abspath $(COVERAGE_OBJ_DIR)/coverage-data)" GCOV_PREFIX_STRIP=0
COVERAGE_COLLECT_ARGS := --gcov-profile-root "$(COVERAGE_OBJ_DIR)/coverage-data" --gcov-tool "$(GCOV_TOOL)"
endif

.PHONY: _test _test-run _coverage-inventory _bench
test: _test
_test: $(TEST_BINS) $(TEST_PROGRAMS)
	@mkdir -p "$(TEST_TMP_ROOT)"
	@SIN_TEST_TMP_ROOT="$(TEST_TMP_ROOT)" TF_TMP_ROOT="$(TEST_TMP_ROOT)" PYTHONDONTWRITEBYTECODE=1 python3 tests/inventory/audit.py --archive "$(LIB)" >/dev/null
	@SIN_TEST_TMP_ROOT="$(TEST_TMP_ROOT)" TF_TMP_ROOT="$(TEST_TMP_ROOT)" TMPDIR="$(TEST_TMP_ROOT)" bash tests/inventory/test_audit.sh "$(LIB)"
	@SIN_TEST_TMP_ROOT="$(TEST_TMP_ROOT)" TF_TMP_ROOT="$(TEST_TMP_ROOT)" TMPDIR="$(TEST_TMP_ROOT)" PYTHONDONTWRITEBYTECODE=1 python3 tests/baseline/audit_baseline.py
	@SIN_TEST_TMP_ROOT="$(TEST_TMP_ROOT)" TF_TMP_ROOT="$(TEST_TMP_ROOT)" TMPDIR="$(TEST_TMP_ROOT)" PYTHONDONTWRITEBYTECODE=1 python3 tests/coverage/test_coverage_gate.py
	@SIN_TEST_TMP_ROOT="$(TEST_TMP_ROOT)" TF_TMP_ROOT="$(TEST_TMP_ROOT)" TMPDIR="$(TEST_TMP_ROOT)" $(TEST_RUNNER_ENV) ./$(FRAMEWORK_RUNNER_BIN) $(addprefix ./,$(TEST_RUN_LIST))
	@SIN_TEST_TMP_ROOT="$(TEST_TMP_ROOT)" TF_TMP_ROOT="$(TEST_TMP_ROOT)" TMPDIR="$(TEST_TMP_ROOT)" $(TEST_RUNNER_ENV) ./$(FRAMEWORK_SELF_BIN) --run runner_discovery_and_jobs
	@export SIN_TEST_TMP_ROOT="$(TEST_TMP_ROOT)" TF_TMP_ROOT="$(TEST_TMP_ROOT)" TMPDIR="$(TEST_TMP_ROOT)"; $(TEST_PROGRAM_ENV); tmp_dir="$(TEST_TMP_ROOT)"; mkdir -p "$$tmp_dir"; tmp_file="$$tmp_dir/duplicate.log"; \
		if ./$(FRAMEWORK_RUNNER_BIN) ./$(FRAMEWORK_SELF_BIN) ./$(FRAMEWORK_DUP_BIN) >"$$tmp_file" 2>&1; then \
			cat "$$tmp_file"; printf '%s\n' 'duplicate discovery unexpectedly succeeded' >&2; exit 1; \
		fi; grep -F 'TF|ERROR|discovery' "$$tmp_file" >/dev/null

test-sanitize:
	+ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=1" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" $(MAKE) --no-print-directory BUILD=sanitize _test
	@printf '%s\n' 'ASan/UBSan deterministic suite completed; leak detection was enabled (run outside ptrace-restricted environments).'

test-full:
	+$(MAKE) --no-print-directory BUILD=debug _test
	+$(MAKE) --no-print-directory BUILD=release _test
	+$(MAKE) --no-print-directory _coverage-inventory
	+ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=1" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" $(MAKE) --no-print-directory BUILD=sanitize _test
	+$(MAKE) --no-print-directory _test-fuzz

_coverage-inventory:
	@mkdir -p "$(COVERAGE_OBJ_DIR)/coverage-data"
	@find "$(COVERAGE_OBJ_DIR)" -type f -name '*.gcda' -delete
	@find "$(COVERAGE_OBJ_DIR)/coverage-data" -mindepth 1 -delete
	+$(COVERAGE_RUN_ENV) LLVM_PROFILE_FILE="$(abspath $(COVERAGE_OBJ_DIR)/coverage-data)/%m.profraw" $(MAKE) --no-print-directory BUILD=coverage _test
	@PYTHONDONTWRITEBYTECODE=1 python3 tests/coverage/coverage_gate.py --build-dir "$(COVERAGE_OBJ_DIR)" --compiler "$(CC)" --archive "$(COVERAGE_LIB_DIR)/libsinshared.a" --gcov "$(GCOV)" --llvm-cov "$(LLVM_COV)" --llvm-profdata "$(LLVM_PROFDATA)" $(COVERAGE_COLLECT_ARGS)

_bench: $(REWRITE_GROUP2_BINS)
	@SIN_EXTENDED_BENCH=1 SIN_BENCH_REPORT=1 ./$(OBJ_DIR)/$(REWRITE_DIR)/test_runtime_benchmark
$(TEST_OBJECT_DIR)/%.o: %.c $(PARSER_H)
	@mkdir -p $(@D)
	$(CC) -c $(TEST_CPPFLAGS) $(TEST_CFLAGS) -MMD -MP -MF $(@:.o=.d) $< -o $@

-include $(TEST_OBJECTS:.o=.d)
