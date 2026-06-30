#!/usr/bin/env bash
set -euo pipefail

# Keep sanitizer behavior consistent between local runs and CI.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:strict_string_checks=1:abort_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"

# Keep gate runtime/toolchain knobs configurable for developers and CI.
export FUZZ_RUNS="${FUZZ_RUNS:-1000}"
export FUZZ_TIME="${FUZZ_TIME:-30}"
export CC="${CC:-gcc}"
export FUZZ_CC="${FUZZ_CC:-clang}"

echo "==> strict warnings build/test"
make clean
make STRICT_WARNINGS=1 CC="${CC}" test

echo "==> address/undefined sanitizer build/test"
make CC="${CC}" test-asan

echo "==> leak sanitizer build/test"
make CC="${CC}" test-lsan

echo "==> fuzz build"
make FUZZ_CC="${FUZZ_CC}" FUZZ_RUNS="${FUZZ_RUNS}" FUZZ_TIME="${FUZZ_TIME}" fuzz-build

echo "==> seeded fuzz smoke run"
make FUZZ_CC="${FUZZ_CC}" FUZZ_RUNS="${FUZZ_RUNS}" FUZZ_TIME="${FUZZ_TIME}" FUZZ_SEED=1 fuzz-smoke-run
