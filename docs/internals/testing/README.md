# Testing Internals

Sinistra's tests use one self-contained C17/POSIX framework. This guide is the
starting point for changing or extending the suite.

## Guide Map

- [Framework architecture](framework.md) explains discovery, execution,
  isolation, records, and cleanup.
- [APIs and ownership](apis.md) covers assertions, processes, fixtures, hooks,
  and shared helper boundaries.
- [Adding and running tests](workflow.md) gives the exact workflow for native,
  conformance, inventory, fixture, network, coverage, and fuzz changes.

## Authoritative Test Documents

These documents are checked-in inputs or policy owned by the test suite:

- [`tests/TEST_COVERAGE.md`](../../../tests/TEST_COVERAGE.md) — coverage and
  group overview.
- [`tests/baseline/README.md`](../../../tests/baseline/README.md) — reviewed
  coverage snapshots and floors.
- [`tests/fixtures/README.md`](../../../tests/fixtures/README.md) — fixture
  classes, naming, and regeneration policy.
- [`tests/fixtures/conformance/README.md`](../../../tests/fixtures/conformance/README.md)
  — manifest schema and conformance expectations.
- [`tests/inventory/README.md`](../../../tests/inventory/README.md) — inventory
  catalogs and audit rules.
- [`tests/rewrite/README.md`](../../../tests/rewrite/README.md) — native adapter
  ownership and descriptor layout.
- [`tests/rewrite/group1/README.md`](../../../tests/rewrite/group1/README.md)
  — Group 1 adapter layout.

The checked-in catalogs and current adapter descriptors are the authoritative
inventory for executable tests and their contract edges. Coverage snapshots
remain separate baseline data used by the coverage-floor checks.

## Quick Commands

```sh
make test                         # normal deterministic gate
TEST_JOBS=4 make test             # parallel non-exclusive batches
TF_VERBOSE=1 make test            # replay passing child output
./obj/debug-gcc/tests/framework/framework-selftest --list
./obj/debug-gcc/tests/framework/framework-selftest --run assertion_equal
```

Use `CC=clang`, `BUILD=release`, or `BUILD=sanitize` for the corresponding
profile. Sanitizer and fuzz gates are intentionally separate from the normal
cleanup workflow; see [Adding and running tests](workflow.md).
