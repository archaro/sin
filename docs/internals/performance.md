# Performance and Benchmarking

This page is the durable index for performance work. It records supported
entry points and evidence policy; timings belong in a dated report such as
[performance-pre-0.8.0.md](performance-pre-0.8.0.md). Reserve
`performance-0.8.0.md` for evidence captured from the released version.

## Deterministic Benchmark-tagged Checks

Benchmark-tagged descriptors are included in the normal `make test` aggregate.
The framework marks them `benchmark,exclusive`, runs each in a fresh process,
and serializes them. They are regression checks for deterministic counters and
invariants, not portable elapsed-time promises. The item-cache descriptor is
also exercised by the normal test suite.

## Release Benchmark Entry Point

Run the supported release benchmark target from the repository root:

```sh
make bench
```

The target delegates to the release build, selects the
`rewrite.runtime.test_runtime_benchmark_optin` descriptor, and sets/retains
the existing `SIN_EXTENDED_BENCH=1 SIN_BENCH_REPORT=1` environment contract.
`SIN_EXTENDED_BENCH=1` enables the extended matrix for this entry point;
benchmark stdout may be captured by the caller.

## Focused Selection and Opt-in Measurements

Build the release benchmark binary, then select its descriptor directly:

```sh
make BUILD=release obj/release-gcc/tests/rewrite/test_runtime_benchmark
SIN_EXTENDED_BENCH=1 \
  ./obj/release-gcc/tests/rewrite/test_runtime_benchmark \
  --run rewrite.runtime.test_runtime_benchmark_optin
```

`SIN_EXTENDED_BENCH=1` enables the extended matrix (network, lists, itemstore,
item references/syscalls, runtime verification, and string-registry samples).
`SIN_STRICT_BENCH=1` additionally enforces the small deterministic threshold
checks in the descriptor. Without the extended flag, the descriptor still
runs its baseline deterministic measurements.

## Adding Evidence

Keep benchmark output machine- and revision-specific. Add a new dated
`performance-<version-or-date>.md` report with the build/compiler/platform,
workload shape, sample policy, deterministic counters, and measured values;
link it from this index and from the internals index. State whether a result is
a regression threshold or an opt-in measurement. Do not turn one machine's
elapsed timing into a portable guarantee.

The checked-in
[performance-pre-0.8.0.md](performance-pre-0.8.0.md) is the historical
pre-release evidence record. Its timings are local historical measurements,
not portable benchmarks or current performance guarantees.
