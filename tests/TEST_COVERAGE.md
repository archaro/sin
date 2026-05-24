# Test Coverage Map

This document maps major subsystems to concrete test entry points so reviewers can quickly verify what is covered and what remains intentionally out of scope.

## core

### Covered entry points
- **Harness and suite wiring**
  - `tests/shared/test_compiler.c`
    - `main(...)`
    - `run_suite(...)`
    - Core suite registration (`core_tests[]`)
- **Fixture contract integrity / regeneration policy**
  - `tests/shared/test_fixture_policy.c`
    - `test_fixture_policy_registry_integrity`
    - `test_fixture_policy_files_present`
    - `test_fixture_policy_no_generated_obj_tracked`
- **Parser / AST / semantic edge behavior**
  - `tests/core/test_absyn_lifecycle.c`
  - `tests/core/test_semant.c`
  - `tests/core/test_parser_input_api.c`
  - `tests/core/test_relative_item_leading_dot.c`
- **IR/core metadata and cache behavior**
  - `tests/core/test_ir_validate.c`
  - `tests/core/test_opcode_schema.c`
  - `tests/core/test_item_cache.c`

### Known gaps
- No property/fuzz-style parser robustness test in-tree (seeded random corpus/replay).
- No dedicated "core malformed-source matrix" test file; malformed-source cases are still distributed across compiler pipeline tests.

## compiler

### Covered entry points
- **Bytecode emission (`emitbc`)**
  - `tests/compiler/test_emitbc_header.c`
    - `test_emitbc_header`
  - `tests/compiler/test_emitbc_opcode_map.c`
    - `test_emitbc_opcode_map`
    - `test_emitbc_opcode_map_call_item_deref_alias_layout`
    - `test_emitbc_opcode_map_unsupported_ir_op`
  - `tests/compiler/test_emitbc_jumps.c`
    - `test_emitbc_jumps`
  - `tests/compiler/test_emitbc_invariants.c`
    - `test_emitbc_invariants`
  - `tests/compiler/test_emitbc_all_ir_ops_accounted_for.c`
    - `test_emitbc_all_ir_ops_accounted_for` (inventory guard for IR-op/schema coverage drift)
- **Pipeline / golden outputs**
  - `tests/compiler/test_pipeline_golden.c`
    - `test_pipeline_golden`
    - `test_pipeline_large_local_lookup_duplicate`
  - `tests/compiler/test_pipeline_source_golden.c`
    - `test_pipeline_source_golden`
  - `tests/compiler/test_pipeline_negative_matrix.c`
    - `test_pipeline_negative_matrix` (parser, semantic, IR-validate, emitter negative-stage checks)
- **Compiler context/diagnostics and tool parity**
  - `tests/compiler/test_compiler_context_failures.c`
    - `test_compiler_context_failures`
  - `tests/compiler/test_compiler_diag_pipeline.c`
    - `test_compiler_diag_pipeline`
  - `tests/compiler/test_parser_examples_obj_golden.c`
    - `test_parser_examples_obj_golden`
  - `tests/compiler/test_sdiss_fixtures.c`
    - `test_sdiss_fixture_basic`
    - `test_sdiss_reads_compiler_operand_widths`

### Known gaps
- The negative matrix now includes representative multi-fault priority checks, but does not exhaustively enumerate all nested control-flow/libcall-typing combinations.

## runtime

### Covered entry points
- **Interpreter semantics golden contracts**
  - `tests/interpreter/test_interpret_semantics_golden.c`
    - `test_interpret_semantics_golden`
- **Interpreter stress / determinism under repeated runs**
  - `tests/interpreter/test_interpret_stress.c`
    - `test_interpret_stress`
- **Libcall registry lifecycle and safety contracts**
  - `tests/core/test_libcall_registry.c`
    - `test_libcall_registry_roundtrip`
    - `test_libcall_registry_init_failure_has_no_partial_state`
    - `test_libcall_registry_lifecycle_reinit_sequence`
    - `test_libcall_registry_repeated_teardown_is_safe`
    - `test_libcall_name_duplicate_detection`
    - `test_missing_libcall_is_null_and_interpret_deterministic`
    - `test_libcall_registry_self_check_invalid_entries`
    - `test_libcall_invalid_arg_branches_return_contracts`
    - `test_net_write_ignores_disconnected_lines`
    - `test_net_write_ignores_non_writable_line_states`
- **Compiler/runtime integration for system libcall execution**
  - `tests/compiler/test_sys_compile_libcall.c`
    - `test_sys_compile_libcall_runtime`
- **Runtime performance guard (opt-in strict mode)**
  - `tests/interpreter/test_runtime_benchmark_optin.c`
    - `test_runtime_benchmark_optin`
    - strict thresholds enabled with `SIN_STRICT_BENCH=1`

### Known gaps
- Runtime stress currently covers selected fixtures (`chat_boot`, `echo_boot`) and validates deterministic outputs across repeated runs; broader fixture/session diversity can still be expanded.
- Performance guard is intentionally opt-in and environment-sensitive; threshold stability across heterogeneous CI hardware is not guaranteed.
