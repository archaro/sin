# Test Coverage Map

This document maps major subsystems to concrete test entry points so reviewers can quickly verify what is covered and what remains intentionally out of scope.

## core

### Covered entry points
- **Harness and suite wiring**
  - `tests/shared/test_compiler.c`
    - `main(...)`
    - `run_suite(...)`
    - Per-test and per-suite elapsed-time reporting
    - Assertion-failure suite/test context via `tests/test_assert.h`
    - Core suite registration (`core_tests[]`)
    - Suite registration validation rejects null/duplicate test entries before execution.
- **Fixture contract integrity / regeneration policy**
  - `tests/shared/test_fixture_policy.c`
    - `test_fixture_policy_declared_goldens_exist`
      - Verifies hand-declared source/sdiss/expected fixtures exist and carry policy metadata.
      - Verifies pipeline golden case names and fixture paths are unique.
      - Verifies every shared pipeline golden fixture exists directly from `pipeline_golden_cases(...)`, avoiding a duplicate stale fixture list.
- **Parser / AST / semantic edge behavior**
  - `tests/core/test_absyn_lifecycle.c`
  - `tests/core/test_semant.c`
  - `tests/core/test_parser_input_api.c`
  - `tests/core/test_parser_float_literals.c`
    - `test_parser_float_literals_decimal_forms`
    - `test_parser_float_literals_integer_still_int`
    - `test_parser_float_literals_item_layers_unchanged`
    - `test_parser_float_literals_malformed_rejected`
    - `test_floatconv_binary64_formatting`
    - `test_floatconv_binary64_format_roundtrip`
    - `test_floatconv_binary64_edge_cases`
  - `tests/core/test_relative_item_leading_dot.c`
    - `test_float_item_literal_layer_rejected_at_compile_time`
    - `test_float_local_deref_layer_returns_nil_and_does_not_save_item`
- **IR/core metadata and cache behavior**
  - `tests/core/test_ir_validate.c`
    - `test_lower_float_value_emits_push_float` (float compiler/lowering coverage)
  - `tests/core/test_opcode_schema.c`
  - `tests/core/test_item_cache.c`
  - `tests/core/test_itemstore_io.c`
    - `test_itemstore_value_and_code_roundtrip`
    - `test_itemstore_nested_depth_roundtrip`
    - `test_itemstore_loads_generated_v1_wire_fixture`
    - `test_load_itemstore_rejects_bad_headers`
    - `test_load_itemstore_rejects_invalid_wire_tags`
    - `test_load_itemstore_rejects_structural_corruption`
    - `test_load_itemstore_rejects_resource_limit_violations`
    - `test_save_itemstore_preserves_existing_file_on_failure`
    - `test_itemstore_durability_modes`
    - `test_itemstore_large_load_presizes_child_storage`
- **Core value/float semantics**
  - `tests/core/test_value_behavior.c`
    - `test_value_push_float_interprets_binary64_payloads`
    - `test_value_float_arithmetic_helpers`
    - `test_value_float_arithmetic_interpreter_bytecode`
    - `test_value_float_construction_copy_truthiness_cleanup`
    - `test_value_comparison_float_ieee754_helpers`

### Known gaps
- No property/fuzz-style parser robustness test in-tree (seeded random corpus/replay).
- Malformed-source cases are centralized in the compiler pipeline negative matrix rather than split between source-golden and negative tests.

## compiler

### Covered entry points
- **Bytecode emission (`emitbc`)**
  - `tests/compiler/test_emitbc_header.c`
    - `test_emitbc_header`
  - `tests/compiler/test_emitbc_opcode_map.c`
    - `test_emitbc_opcode_map`
    - `test_emitbc_opcode_map_call_item_deref_alias_layout`
    - `test_emitbc_opcode_map_unsupported_ir_op`
    - `test_emitbc_push_float_immediate_layout` (`IR_OP_PUSH_FLOAT` opcode byte and 8-byte payload layout)
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
    - `test_pipeline_negative_matrix` (centralized parser and semantic failure checks plus boolean/truthiness source nonregressions)
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
    - `test_interpret_semantics_golden` (includes VM numeric semantics such as mixed int/float promotion, NaN/signed-zero comparisons, float truthiness, and float formatting)
- **Interpreter stress / determinism under repeated runs**
  - `tests/interpreter/test_interpret_stress.c`
    - `test_interpret_stress`
- **Libcall registry lifecycle and safety contracts**
  - `tests/core/test_libcall_registry.c`
    - `test_libcall_registry_roundtrip`
    - `test_libcall_registry_init_failure_has_no_partial_state`
    - `test_libcall_registry_lifecycle_reinit_sequence`
    - `test_libcall_registry_repeated_teardown_is_safe`
    - `test_default_libcall_wrappers_lazy_init_after_reset`
    - `test_missing_libcall_is_null_and_interpret_deterministic`
    - `test_libcall_registry_self_check_invalid_entries`
    - `test_libcall_invalid_arg_branches_return_contracts`
    - `test_libcall_float_integer_only_arguments_rejected`
    - `test_str_libcalls_float_returns_invalidargs_nil`
    - `test_str_len_returns_string_byte_length`
    - `test_str_valtostr_converts_values_to_strings`
    - `test_str_case_libcalls_mutate_strings_in_place`
    - `test_str_trim_libcalls_return_trimmed_strings`
    - `test_str_substr_returns_requested_byte_range`
    - `test_str_substr_invalid_args_return_nil`
    - `test_str_find_and_contains_return_expected_results`
    - `test_str_find_and_contains_invalid_args_return_contracts`
    - `test_str_startswith_and_endswith_return_expected_results`
    - `test_str_startswith_and_endswith_invalid_args_return_contracts`
    - `test_str_eqcasei_returns_expected_results`
    - `test_str_eqcasei_invalid_args_return_contracts`
    - `test_str_replace_returns_expected_results`
    - `test_str_replace_invalid_args_return_nil`
    - `test_str_repeat_returns_expected_results`
    - `test_str_repeat_invalid_args_return_nil`
    - `test_str_growth_libcalls_enforce_string_limit`
    - `test_str_padleft_and_padright_return_expected_results`
    - `test_str_padleft_and_padright_invalid_args_return_nil`
    - `test_str_libcall_invalidargs_uses_context_itemroot`
    - `test_libcall_output_formats_values`
    - `test_net_write_ignores_non_writable_lines`
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

## documentation

### Covered entry points
- **Float user-facing and maintainer-facing docs**
  - `docs/concepts.md` documents float literal syntax, binary64/IEEE 754 semantics, mixed numeric promotion, NaN comparisons, signed-zero comparison/formatting, and float truthiness.
  - `docs/bytecode.md` documents `IR_OP_PUSH_FLOAT`, opcode byte `P`, the 8-byte binary64 immediate payload, and endian/platform assumptions.
  - `docs/libcalls.md` documents float logging/writing behavior and float argument validation for libcalls.

### Known gaps
- Documentation coverage is maintained by review rather than an automated doc-lint that verifies every float behavior sentence against tests.

## Network tests

The dedicated `make test-network` target builds `tests/network/test_network.c`, which
includes `src/net/network.c` with local libuv and libtelnet stubs. These tests assume
that connection-management behavior can be validated without opening real sockets:
libuv accept/read/write/close calls are captured by stubs, write callbacks are
invoked explicitly by the test, and allocation/telnet failures are injected by the
harness. The adversarial long-stream case exercises the input buffering limits in
process rather than through the kernel socket stack, so it verifies bounded server
buffering and disconnect state without requiring network access.

The `make test-chat-smoke` target builds `tests/network/test_chat_smoke.c` and
runs the real `examples/chat-boot.src` / `examples/chat-load.src` flow through
`scomp`, `sin`, a temporary itemstore, and localhost sockets. It checks that the
server accepts a connection, runs the input item for connect/input/`\quit`,
flushes the quit message before disconnecting, and exits through `sys.shutdown`.
