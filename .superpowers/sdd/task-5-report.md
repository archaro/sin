# Task 5 implementation report

## Implementation

Reworked `sin_list_concat()` to use the existing borrowed leaf iterator and
node reference counts. Empty-side retains remain constant-time. Aligned left
inputs retain complete RHS root leaves and tails. Unaligned joins clone only
the left/right boundary fragments needed to form complete root blocks and
retain the unaffected RHS leaves; new branch spines are rebuilt with existing
refcount ownership rules. Added deterministic leaf-identity coverage and
aligned/unaligned benchmark rows.

## Files changed

- `src/runtime/list.c`
- `tests/core/test_list.c`
- `tests/interpreter/test_runtime_benchmark_optin.c`
- `tests/shared/test_harness.c`
- `.superpowers/sdd/task-5-report.md`

The pre-existing untracked `pre-0.8.0.txt` was preserved and is not part of
the commit.

## RED/GREEN evidence

- RED: after adding `test_list_concat_shares_rhs_leaves`, the existing concat
  implementation failed at `tests/core/test_list.c:85` while looking for an
  RHS leaf identity in the result.
- GREEN: after the concat rewrite, the focused suite and full harness passed;
  the final standard run was `367/367`.

## Validation

- ✅ `make test` — `367` passed, `0` failed.
- ✅ `make test-warnings` — strict-warning build and suite passed (`367/367`).
- ✅ `make test-release` — release/strict-warning build and suite passed
  (`367/367`).
- ✅ `make test-asan` — ASan/UBSan with leak checks disabled passed
  (`367/367`).
- ✅ `make test-lsan` (run outside the restricted sandbox) — ASan/UBSan with
  leak checks enabled passed (`367/367`).
- ✅ `make test-benchmark` — optimized opt-in matrix passed (`339/339`).
- ✅ `git diff --check` — clean.
- ⚠️ A second `make -s test-benchmark 2>&1 | rg ...` attempt was aborted after
  the initial short poll produced no output; it was rebuilding, not hung. The
  complete benchmark run immediately before it finished successfully.

## Benchmark comparison

The completed optimized run reported:

- Existing concat size 1024: `310 ns/invocation` versus the supplied baseline
  `33224 ns/invocation`.
- New rows: size 1056, `165 ns` aligned and `276 ns` unaligned; size 2048,
  `320 ns` aligned and `322 ns` unaligned.
- Clone/release: `4 ns/invocation` at sizes 0, 8, and 1024, matching baseline.
- Set: `114` at size 8 and `507` at size 1024 versus supplied `128` and `500`.
- Append: `247`, `82`, `275`, and `211 ns/invocation` at inputs 31, 32,
  1055, and 1056. These are within normal timing noise except the latter two
  are slightly above the supplied single-run medians; append code was not
  changed, so this requires repeated-run comparison before treating it as a
  regression.

## Self-review and acceptance status

The tests cover 31/32/33 and 1023/1024/1025 concat boundaries, self-concat,
shared inputs, allocation-failure retry behavior with unchanged inputs,
borrowed RHS leaf identity, and releasing inputs/results in either order.
Existing list tests cover nested list ownership and recursive depth behavior;
the full libcall/interpreter suites also remain green. Failure paths in the
new retained-node and boundary construction stages release owned nodes once,
and `list_new()` handles final list allocation failure.

All functional acceptance criteria are satisfied by deterministic tests and
sanitizer validation. The benchmark no-regression criterion is not conclusively
established from one noisy run; repeated benchmark medians should be used for
that performance gate.
