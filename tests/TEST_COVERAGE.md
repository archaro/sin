# Test Coverage Map

This document maps major subsystems to concrete test entry points so reviewers
can quickly verify what is covered and what remains intentionally out of scope.

The unified test harness (`tests/shared/test_harness.c`) builds a single
`tests/test-suite` binary that registers three logical suites:

| Suite     | Tests | Source files (selected) |
|-----------|-------|-------------------------|
| core      |   112 | `tests/core/`           |
| compiler  |    31 | `tests/compiler/`       |
| runtime   |    83 | `tests/core/`, `tests/interpreter/` |
| **Total** |**226**|                         |

## core

### Covered entry points

- Itemstore lifecycle and cache isolation are covered by
  `test_itemstore_cache_state_is_store_local`, including independent
  generations, hit/miss counters, mutation invalidation, and destruction of
  one store while another remains usable.
- Execution lifetime is covered by
  `test_item_execution_pins_are_balanced_and_zero_safe`,
  `test_insert_code_item_rejects_inuse_replacement`, and
  `test_delete_item_rejects_pinned_descendant`: balanced frame pins protect
  payloads, and ancestor deletion preserves generation/cache state until a
  pinned descendant leaves.
- **Harness and suite wiring**
  - `tests/shared/test_harness.c`
    - `main(...)`
    - `run_suite(...)`
    - Per-test and per-suite elapsed-time reporting
    - Assertion-failure suite/test context via `tests/test_assert.h`
    - Core suite registration (`core_tests[]` with 112 entries)
    - Suite registration validation rejects null/duplicate test entries
      before execution.
- **Fixture contract integrity / regeneration policy**
  - `tests/shared/test_fixture_policy.c`
    - `test_fixture_policy_declared_goldens_exist`
      - Verifies hand-declared source/sdiss/expected fixtures exist and carry
        policy metadata.
      - Verifies pipeline golden case names and fixture paths are unique.
      - Verifies every shared pipeline golden fixture exists directly from
        `pipeline_golden_cases(...)`, avoiding a duplicate stale fixture list.
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
    - `test_lower_float_value_emits_push_float` (float compiler/lowering
      coverage)
  - `tests/core/test_opcode_schema.c`
  - `tests/core/test_item_cache.c`
    - `test_murmur3_32_alignment_and_vectors` (deterministic MurmurHash3
      vectors, including complete blocks, tails, a non-zero seed, and an
      unaligned input address)
    - `test_find_item_cached_rejects_invalid_names_without_counters`
    - `test_find_item_cached_relative_invalid_name_preserves_counters`
    - `test_itemstore_benchmarks` (cached/uncached and negative lookups,
      shallow/max-depth paths, sibling cardinalities, payload replacement
      cache effects, insertion/deletion/ordered iteration, and loaded versus
      runtime-constructed stores; observational timings only)
  - `tests/core/test_itemstore_io.c`
    - `test_itemstore_value_and_code_roundtrip`
    - `test_loaded_itemstore_mutation_roundtrip`
    - `test_itemstore_nested_depth_roundtrip`
    - `test_itemstore_item_name_contract_boundaries_roundtrip`
    - `test_itemstore_item_name_rejection_is_atomic`
    - `test_itemstore_item_name_relative_depth_contract`
    - `test_save_itemstore_rejects_manually_invalid_item_names`
    - `test_itemstore_loads_generated_v1_wire_fixture`
    - `test_load_itemstore_rejects_bad_headers`
    - `test_load_itemstore_rejects_invalid_wire_tags`
    - `test_load_itemstore_rejects_structural_corruption`
    - `test_load_itemstore_rejects_resource_limit_violations`
    - `test_save_itemstore_preserves_existing_file_on_failure`
    - `test_itemstore_durability_modes`
    - `test_itemstore_large_load_presizes_child_storage`
    - Loaded itemstore mutation after persistence: root-level children, nested
      children, code items, deletion, reinsertion, and ordered name enumeration
      before and after a second save/load cycle.
- **Core value/float semantics**
  - `tests/core/test_value_behavior.c`
    - `test_value_push_float_interprets_binary64_payloads`
    - `test_value_float_arithmetic_helpers`
    - `test_value_float_arithmetic_interpreter_bytecode`
    - `test_value_float_construction_copy_truthiness_cleanup`
    - `test_value_comparison_float_ieee754_helpers`

### Known gaps
- Compiler fuzzing is available through `tests/fuzz/fuzz_scomp.c`,
  `make fuzz-scomp`, and `FUZZ_SEED=1 make fuzz-smoke`; the remaining gap is
  limited corpus and campaign diversity rather than the absence of a fuzz
  harness.
- Malformed-source cases are centralized in the compiler pipeline negative
  matrix rather than split between source-golden and negative tests.

## compiler

### Covered entry points
- **Bytecode emission (`emitbc`)**
  - `tests/compiler/test_emitbc_header.c`
    - `test_emitbc_header`
  - `tests/compiler/test_emitbc_opcode_map.c`
    - `test_emitbc_opcode_map`
    - `test_emitbc_opcode_map_call_item_deref_alias_layout`
    - `test_emitbc_opcode_map_unsupported_ir_op`
    - `test_emitbc_push_float_immediate_layout` (`IR_OP_PUSH_FLOAT` opcode byte
      and 8-byte payload layout)
  - `tests/compiler/test_emitbc_jumps.c`
    - `test_emitbc_jumps`
  - `tests/compiler/test_emitbc_invariants.c`
    - `test_emitbc_invariants`
  - `tests/compiler/test_emitbc_all_ir_ops_accounted_for.c`
    - `test_emitbc_all_ir_ops_accounted_for` (inventory guard for
      IR-op/schema coverage drift)
- **Pipeline / golden outputs**
  - `tests/compiler/test_pipeline_golden.c`
    - `test_pipeline_golden`
    - `test_pipeline_large_local_lookup_duplicate`
  - `tests/compiler/test_pipeline_source_golden.c`
    - `test_pipeline_source_golden`
  - `tests/compiler/test_pipeline_negative_matrix.c`
    - `test_pipeline_negative_matrix` (centralized parser and semantic failure
      checks plus boolean/truthiness source nonregressions)
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
- The negative matrix now includes representative multi-fault priority checks,
  but does not exhaustively enumerate all nested control-flow/libcall-typing
  combinations.

## runtime

The runtime suite contains 83 tests registered in `runtime_tests[]`.
Twelve tests come
from `tests/core/test_value_behavior.c` and exercise the decoder, interpreter
contracts, and strict-validation machinery directly. The remaining tests cover
interpreter golden contracts, libcall registries, per-library call contracts,
task lifecycle and introspection, networking, string operations, `sys.compile`
integration, and an opt-in performance guard.

### Covered entry points
- **Runtime decoder, interpreter contracts, and strict validation** (12 tests,
  from `tests/core/test_value_behavior.c`)
  - `test_runtime_decode_requires_frame_bounds`
  - `test_interpreter_truncated_single_byte_operands`
  - `test_assigncodeitem_rejects_malformed_source_block_with_runtime_bytecode_error`
  - `test_assigncodeitem_rejects_invalid_target_name_type_with_runtime_item_error`
  - `test_strict_runtime_contracts_default_preserves_fetch_argument_drops`
  - `test_strict_validation_alone_preserves_fetch_argument_drops`
  - `test_strict_runtime_contracts_reports_too_many_item_arguments`
  - `test_strict_runtime_contracts_reports_invalid_item_name_arguments`
  - `test_strict_runtime_contracts_reports_missing_item_arguments`
  - `test_strict_runtime_contracts_uses_context_itemroot`
  - `test_strict_validation_runtime_opt_in`
  - `test_strict_validation_rejects_null_bytecode`
- **Interpreter semantics golden contracts**
  - `tests/interpreter/test_interpret_semantics_golden.c`
    - `test_interpret_semantics_golden` (includes VM numeric semantics such as
      mixed int/float promotion, NaN/signed-zero comparisons, float
      truthiness, and float formatting)
    - `test_interpret_result_semantics`
      - source compilation and runtime execution of `sys.save` with a loadable
        checkpoint
    - `test_interpret_rejects_malformed_bytecode_before_execution`
    - `test_interpret_baseline_bytecode_safety_in_default_and_strict_modes`
- **Interpreter stress / determinism under repeated runs**
  - `tests/interpreter/test_interpret_stress.c`
    - `test_interpret_stress`
- **Libcall registry lifecycle and safety contracts**
  - `tests/core/test_libcall_registry.c`
    - `test_libcall_registry_roundtrip`
      - `sys.save` package index, call index, arity, token, and handler mapping
      - canonical `sys`-first library grouping, alphabetical remaining
        libraries, ascending per-library call indices, and representative
        token/arity/handler round trips.
    - `test_runtime_init_validates_libcalls_once`
    - `test_libcall_registry_init_failure_has_no_partial_state`
    - `test_libcall_registry_lifecycle_reinit_sequence`
    - `test_libcall_registry_repeated_teardown_is_safe`
    - `test_default_libcall_wrappers_lazy_init_after_reset`
    - `test_missing_libcall_is_null_and_interpret_deterministic`
    - `test_libcall_registry_self_check_invalid_entries`
    - `test_libcall_invalid_arg_branches_return_contracts`
    - `test_libcall_float_integer_only_arguments_rejected`
    - `test_libcall_output_formats_values`
- **System introspection, persistence, version, and clocks (`sys.*` libcalls)**
  - `tests/core/test_libcall_sys.c`
    - `test_sys_introspection_libcalls`
      - top-level/nested current-item and namespace-parent names, including
        missing-current-item defense
      - all public item-type names, current-item-relative lookup,
        invalid/missing names, and invalid-argument provenance
      - child/root counts, leaf zero, and agreement with `sys.nthname` /
        `sys.rootname` enumeration
      - raw version, wall-clock milliseconds, nondecreasing monotonic
        milliseconds, and prior-error preservation
    - `test_sys_persistence_libcalls`
      - primary checkpoint snapshot isolation and loadability
      - context-selected full/fast durability propagation
      - separate timestamped backup creation without primary replacement
      - boolean failure results, structured persistence diagnostics,
        executing-item provenance, and prior-error preservation
      - defensive no-crash behavior for invalid direct C-level calls without a
        root, VM, or stack
    - `test_sys_caller_paramcount_libcalls`
      - direct-entry nil, two- and three-level caller selection, return
        restoration, owned strings, defensive missing context, and prior-error
        preservation
      - zero/multiple declared parameters, absolute/relative names, malformed
        code headers, value/missing/invalid items, and invalid-argument
        provenance
    - `test_sys_source_libcall`
      - exact and empty nested source reads, absolute/relative names,
        independently owned results, and prior-error preservation
      - invalid/missing/value nil outcomes, non-string provenance,
        missing/unconfigured source, oversized files, embedded NUL rejection,
        stack balance, and temporary-tree cleanup
    - `test_sys_wall_milliseconds_boundaries`
    - `test_sys_item_libcalls`
- **Task lifecycle, scheduling, and introspection (`task.*` libcalls)**
  - `tests/core/test_task_lifecycle.c`
    - `test_task_one_shot_auto_retires`
    - `test_task_repeating_execution_and_explicit_kill`
    - `test_task_setup_failures_unwind`
    - `test_task_id_reuse_is_exactly_once`
    - `test_task_finalise_handles_active_and_closing`
  - `tests/core/test_libcall_task.c`
    - lifecycle and scheduling contracts
    - `test_newgametask_rejects_invalid_intervals_before_timer_start`
    - `test_newgametask_rejects_missing_event_loop_before_returning_task_id`
    - introspection contracts (`task.thisid`, `task.exists`, `task.count`)
    - `test_task_introspection_thisid_ordinary_context_returns_nil`
    - `test_task_introspection_exists_valid_and_invalid_ids`
    - `test_task_exists_rejects_non_integer`
    - `test_task_introspection_count_and_exists_with_lifecycle`
    - `test_task_thisid_in_callback_survives_self_close`
    - `test_newgametask_child_callback_uses_own_identity`
- **Networking libcalls (`net.*`)**
  - `tests/core/test_libcall_net.c`
    - `net.maxlines`
      - `test_net_maxlines_returns_configured_slot_bound`
    - `net.connected`
      - `test_net_connected_reports_writable_telnet_states`
      - `test_net_connected_invalid_line_returns_nil`
    - `net.address`
      - `test_net_address_returns_owned_numeric_peer_address`
      - `test_net_address_invalid_line_returns_nil`
    - `net.write`
      - `test_net_write_ignores_non_writable_lines`
    - `net.flush`
      - `test_net_flush_reports_line_status`
    - `net.ditch`
      - `test_net_ditch_disconnects_active_lines`
      - `test_net_ditch_reports_inactive_lines`
      - `test_net_ditch_invalid_line_returns_nil`
    - `net.input`
      - `test_net_input_fair_queue_progresses_connect_data_disconnect`
    - `net.echo`
      - `test_net_echo_negotiates_current_line_and_consumes_values`
      - `test_net_echo_ignores_unavailable_current_line`
- **String libcalls (`str.*`)**
  - `tests/core/test_libcall_str.c`
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
    - `test_str_libcalls_float_returns_invalidargs_nil`
- **Compiler/runtime integration (`sys.compile`)**
  - `tests/core/test_libcall_sys_compile.c`
    - `test_sys_compile_libcall_runtime`
      - exact compiled `sys.log` output, scalar assignment, and parameterized
        code invocation
      - compile diagnostics, runtime errors, handled errors, and prior-error
        clearing
      - interrupted nested execution, frame unwinding, and temporary-item
        cleanup
      - collision-safe temporary names preserve existing items and descendants
      - source-level execution of all introspection/version/time calls,
        including nested callee identity, namespace parents, and caller
        restoration
      - source-level immediate caller identity across two and three levels,
        temporary `sys.compile` caller behavior, and zero/multiple parameter
        counts
      - source-level compilation and runtime dispatch of `sys.source` against
        the canonical itemstore source layout
- **Core stack frames and caller-boundary restoration**
  - `tests/core/test_stack_frames.c` and
    `tests/interpreter/test_interpret_semantics_golden.c`
    - invocation caller-boundary restoration on normal,
      verification-failure, pending-interrupt, and interpretation-failure exits
- **Runtime performance guard (opt-in strict mode)**
  - `tests/interpreter/test_runtime_benchmark_optin.c`
    - `test_runtime_benchmark_optin`
    - strict thresholds enabled with `SIN_STRICT_BENCH=1`
- **Shared libcall fixture support**
  - `tests/shared/test_libcall_support.[ch]` provides private fixtures and
    helpers used by the split libcall test files. It has no standalone harness
    registration.

### Known gaps
- Runtime stress currently covers selected fixtures (`chat_boot`,
  `echo_boot`) and validates deterministic outputs across repeated runs;
  broader fixture/session diversity can still be expanded.
- Performance guard is intentionally opt-in and
  environment-sensitive; threshold stability across heterogeneous CI
  hardware is not guaranteed.

## documentation

### Covered entry points
- **Float user-facing and maintainer-facing docs**
  - `docs/concepts.md` documents float literal syntax, binary64/IEEE 754
    semantics, mixed numeric promotion, NaN comparisons, signed-zero
    comparison/formatting, and float truthiness.
  - `docs/bytecode.md` documents `IR_OP_PUSH_FLOAT`, opcode byte `P`, the
    8-byte binary64 immediate payload, and endian/platform assumptions.
  - `docs/libcalls.md` documents float logging/writing behavior and float
    argument validation for libcalls.

### Known gaps
- Documentation coverage is maintained by review rather than an automated
  doc-lint that verifies every float behavior sentence against tests.

## Network tests (standalone, outside the unified harness)

Two additional test targets exercise network behavior independently of the
unified `tests/test-suite` harness:

- **`make test-network` (libuv and libtelnet stubs)**
  Builds and runs `tests/network/test_network.c`, which includes
  `src/net/network.c` with local libuv and libtelnet stubs. These tests
  validate low-level connection-management state, ownership, and buffer
  behaviour without opening real sockets: libuv accept/read/write/close calls
  are captured by stubs, write callbacks are invoked explicitly by the test,
  and allocation/telnet failures are injected by the harness. The adversarial
  long-stream case exercises input buffering limits in-process. Lifecycle
  cases define active, disconnecting, disconnected, and reusable line states,
  and cover local ditch, remote EOF, repeated disconnect, writes after ditch,
  and line slot reuse.

- **`make test-chat-smoke` (real localhost end-to-end)**
  Builds and runs `tests/network/test_chat_smoke.c` with the real
  `examples/chat-boot.src` / `examples/chat-load.src` flow through `scomp`,
  `sin`, a temporary itemstore, and localhost sockets. It checks that the
  server accepts a connection, runs the input item for connect/input/`\quit`,
  flushes the quit message before disconnecting, and exits through
  `sys.shutdown`.

These network-specific targets complement, but do not perfectly overlap with,
the `net.*` libcall contract tests in `tests/core/test_libcall_net.c`:

- `tests/core/test_libcall_net.c` tests the language-visible `net.*`
  call contracts (argument validation, return-value semantics, error
  provenance) through the runtime libcall adapter. These tests are
  registered in the unified harness's `runtime` suite and run with
  `make test`.
- `tests/network/test_network.c` tests the internal state machine,
  buffer management, and event-callback ownership of `src/net/network.c`
  against stubbed I/O. These tests are built and run by `make test-network`.
- `tests/network/test_chat_smoke.c` exercises the full stack end-to-end
  through real localhost sockets and the Sinistra runtime. These tests are
  built and run by `make test-chat-smoke`.
