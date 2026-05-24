# Test Coverage Map

This document maps major subsystems to concrete test entry points so reviewers can quickly verify what is covered, what is intentionally not covered yet, and what is planned next.

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
- **Parser / AST / semantic edge behavior (core correctness slices)**
  - `tests/core/test_relative_item_leading_dot.c`
    - `test_relative_item_leading_dot` (compile success + runtime contract on missing leading layer)

### Known gaps
- No single consolidated "core smoke" test that exercises parser + symbol resolution + error diagnostics in one pass across a matrix of malformed programs.
- No fuzz/property-style core parser tests in-tree (determinism/robustness under randomized token streams is not directly validated here).

### Planned tests
- Add `test_core_negative_matrix` (new file under `tests/core/`) to centralize malformed-source diagnostics that are currently scattered.
- Add a deterministic parser fuzz harness (seeded corpus replay) to improve confidence in error-recovery paths.

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
    - internal jump-path checks:
      - `test_emitbc_jump_forward_offsets`
      - `test_emitbc_jump_backward_offset_negative`
      - `test_emitbc_jump_label_errors`
      - `test_emitbc_jump_offset_out_of_range`
  - `tests/compiler/test_emitbc_invariants.c`
    - `test_emitbc_invariants`
    - internal invariant checks:
      - `test_emitbc_op_class_invariants`
      - `test_emitbc_determinism_fixed_seed`
      - `test_emitbc_label_heavy_jump_targets_in_bounds`
- **Pipeline / golden outputs**
  - `tests/compiler/test_pipeline_golden.c`
    - `test_pipeline_golden`
    - `test_pipeline_large_local_lookup_duplicate`
  - `tests/compiler/test_pipeline_source_golden.c`
    - `test_pipeline_source_golden`
  - `tests/compiler/test_pipeline_negative_matrix.c`
    - `test_pipeline_negative_matrix`
- **Compiler-facing libcall lookup/dispatch utilities**
  - `tests/compiler/test_libcall_lookup_precomputed.c`
    - `test_libcall_lookup_precomputed`
  - `tests/compiler/test_libcall_dispatch_microbench.c`
    - `test_libcall_dispatch_microbench`

### Known gaps
- Opcode coverage is strong for mapped ops and jump mechanics, but there is no explicit per-opcode mutation test that auto-fails when new IR ops are introduced without matching coverage updates.
- Negative pipeline matrix does not yet appear to encode every combinatorial interaction of nested control-flow + libcall argument typing failures.

### Planned tests
- Add generated opcode inventory test (e.g., `test_emitbc_all_ir_ops_accounted_for`) that compares IR enum inventory with opcode-map expectations.
- Expand negative pipeline matrix with multi-fault programs (type + control-flow + symbol issues) and assert stable diagnostic ordering.

## runtime

### Covered entry points
- **Interpreter semantics golden contracts**
  - `tests/interpreter/test_interpret_semantics_golden.c`
    - `test_interpret_semantics_golden`
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
- **Compiler/runtime integration for system libcall execution**
  - `tests/compiler/test_sys_compile_libcall.c`
    - `test_sys_compile_libcall_runtime`

### Known gaps
- Golden semantics primarily target curated example programs; coverage of long-running stateful runtime sessions and stress-level resource churn is limited.
- Runtime/libcall tests focus on correctness and contract checks, but there is minimal regression guarding around performance envelopes beyond microbench printouts.

### Planned tests
- Add runtime stress test that repeatedly boots/interprets fixture programs in one process and asserts stable memory/resource behavior.
- Add assertions-backed performance budget test (opt-in / non-CI-strict mode) for selected libcall dispatch and interpreter hot paths.
