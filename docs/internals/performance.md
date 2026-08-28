# Performance measurements

Build the release benchmark binary, then invoke its benchmark descriptor
directly:

```sh
make BUILD=release obj/release-gcc/tests/rewrite/test_runtime_benchmark
SIN_EXTENDED_BENCH=1 \
  ./obj/release-gcc/tests/rewrite/test_runtime_benchmark \
  --run rewrite.runtime.test_runtime_benchmark_optin
```

Benchmark-tagged descriptors are process-isolated and serialized by the
framework; they are included in the ordinary deterministic test run. The
extended measurement matrix is opt-in via `SIN_EXTENDED_BENCH=1`. The
top-level `make bench` recipe is stale: it omits the
required `--run` argument and exits with usage. Fix that build rule separately.

The checked-in [performance-0.8.0.md](performance-0.8.0.md) is the historical
evidence record. Its timings are local historical measurements, not portable
benchmarks or current performance guarantees.
