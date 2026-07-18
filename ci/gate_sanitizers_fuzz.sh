#!/usr/bin/env bash
set -euo pipefail

# Allow the gate to be launched from any working directory.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd -P)"
cd "${REPO_ROOT}"

# Keep sanitizer behavior consistent between local runs and CI.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:strict_string_checks=1:abort_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"

# Keep gate runtime/toolchain knobs configurable for developers and CI.
export FUZZ_RUNS="${FUZZ_RUNS:-1000}"
export FUZZ_TIME="${FUZZ_TIME:-30}"
export FUZZ_SEED="${FUZZ_SEED:-1}"
export FUZZ_ARTIFACT_DIR="${FUZZ_ARTIFACT_DIR:-}"
export CC="${CC:-gcc}"
export FUZZ_CC="${FUZZ_CC:-clang}"

echo "==> strict warnings tests"
make CC="${CC}" test-warnings

echo "==> release-mode tests"
make CC="${CC}" test-release

echo "==> leak-detecting sanitizer tests"
make CC="${CC}" test-lsan

echo "==> seeded fuzz smoke"
make FUZZ_CC="${FUZZ_CC}" FUZZ_RUNS="${FUZZ_RUNS}" FUZZ_TIME="${FUZZ_TIME}" \
	FUZZ_SEED="${FUZZ_SEED}" FUZZ_ARTIFACT_DIR="${FUZZ_ARTIFACT_DIR}" fuzz-smoke
