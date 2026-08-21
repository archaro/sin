# Coverage baseline and floors

This directory is the authoritative, checked-in baseline for production
coverage. The C17 framework and current adapter/catalog descriptors own test
execution and contract relationships; this directory contains coverage data
and its reviewed floors.

Files:

- [`coverage_snapshot.csv`](coverage_snapshot.csv) — GCC/gcov counts and
  percentages for every authored production `src/**/*.c` module.
- [`coverage_floors.csv`](coverage_floors.csv) — the active, manually reviewed
  percentage floors enforced by `make test-full`. This is intentionally
  separate from the coverage snapshot and is never rewritten by a test
  command.
- [`coverage_floors_clang.csv`](coverage_floors_clang.csv) — the separately
  reviewed Clang 18 floors. GCC/gcov and LLVM source-coordinate metrics are
  not silently compared as if they were identical; each compiler has an
  explicit complete floor set. Each row carries a toolchain key (`gcc-13` or
  `clang-18`), so a floor cannot be accidentally applied to another major.
- [`audit_baseline.py`](audit_baseline.py) — deterministic validation of
  coverage fields, owners, source-module coverage rows, and count/percentage
  consistency. It reads only checked-in files and source paths; it does not
  require build artifacts.

## Recorded provenance

- Baseline commit: `ae4b9b92283e7e6ac3db61541eb65617db53dac7`
- Baseline date: `2026-08-14` (UTC)
- Compiler: `gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`
- Coverage tool: `gcov (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`

The exact clean and instrumented coverage-gate command was:

```sh
make clean && make test \
  CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -fprofile-arcs -ftest-coverage -O0 -g' \
  LDFLAGS='-fprofile-arcs -ftest-coverage'
```

The coverage collection command, run after that gate, was the following
per-module invocation (with `<module-dir>` and `<module>.c` expanded for every
authored C source):

```sh
gcov -b -f --json-format -o obj/debug-gcc/<module-dir> src/<module>.c
```

## Active coverage gate

Run the complete instrumented workload, contract inventory audit, and floor
comparison with:

```sh
make test-full
```

The target composes a `BUILD=coverage` phase, using GCC's `gcov` instrumentation for GCC
and the corresponding native profile instrumentation for Clang. Machine and
human-readable reports are written below `obj/coverage-<compiler>/coverage/`;
they are generated artifacts and are not checked in. The comparison uses
rounded two-decimal percentage floors for lines, branches, and functions. GCC
and Clang use separate complete floor files (each row also carries a matching
vendor-major toolchain key) because their instrumentation coordinate
definitions differ. A measured zero-total metric is valid only when its
reviewed floor is `n/a`.

GCC coverage selects `gcov-<compiler-major>` by default and Clang selects
version-matched LLVM tools. An explicit `GCOV=...`, `LLVM_COV=...`, or
`LLVM_PROFDATA=...` override is allowed only when the reporter's reported major
matches the selected compiler/toolchain major.

The reviewed floor keys are `gcc-13` for the GCC snapshot and `clang-18` for
the initial Clang baseline. A different compiler major fails closed with a
`no reviewed coverage baseline` error. The Clang floors were reviewed from the
initial LLVM 18 native run on `Ubuntu clang version 18.1.3` using `llvm-cov-18`
and `llvm-profdata-18`. The collector invokes one combined `llvm-cov export`
over the merged profile and all gate binaries, then uses each authored file's
native `summary.lines`, `summary.branches`, and `summary.functions` covered/count
pairs. These floors are a separate LLVM-native baseline, not converted GCC
percentages; LLVM and gcov metrics are intentionally incomparable.

`src/net/libtelnet.c` is third-party-derived and is explicitly recorded as
`excluded_third_party`; it is not silently omitted from the module inventory.
`src/libcall/libcall_table.c` is retained as an explicit
`no_instrumentable_code` static-data-only record. Every other authored C
module is measured and must have exactly one floor record.

Changing a floor is a manual review edit to the applicable compiler-specific
floor file (`coverage_floors.csv` or `coverage_floors_clang.csv`): update the
percentage deliberately and include a nonblank rationale that cites the
reason for the change. The self-test target exercises the auditor's success
and failure cases:

```sh
make test-full
```

The checked-in coverage snapshot was collected with a separate standalone
network translation-unit observation:

```sh
gcov -b -f --json-format \
  -o tests/network/test-network-test_network.gcno \
  tests/network/test_network.c
```

The collector parsed the resulting `<module>.gcov.json.gz` file. A line,
branch, or function counted as covered when its gcov execution count was
greater than zero; totals are the number of JSON observations, and percentages
are calculated from the stored counts rounded to two decimal places. Source
files generated under `obj/<variant>/generated/parser.c` and
`obj/<variant>/generated/lexer.c` (whose canonical authored inputs are
`src/compiler/parser.y` and `src/compiler/lexer.l`), all test translation
units, and non-production generated files are excluded. The static-data-only
`src/libcall/libcall_table.c` is retained with an explicit
`no_instrumentable_code` row.

The production object graph is the authoritative observation for modules linked
into multiple executables. In particular, `network.c` and `libtelnet.c` are
compiled into the production library at `obj/debug-gcc/net/`; the standalone
network test also includes inline copies under its test translation unit. Those
two authored-source observations are unioned without double-counting: lines
use `(canonical source path, line number, function name)`, functions use
`(canonical source path, function name)`, and branches use the source-order
branch ordinal within each function. Before unioning branches, the collector
verified equal counts and matching gcov `throw`/`fallthrough` metadata for each
function; the sole line-attribution difference (`_set_rfc1143`, line 345 in
the library object versus 343 in the inline copy) therefore uses the matching
branch ordinal rather than inventing a second branch. A positive hit in either
observation marks the unique coordinate covered, and totals count unique
coordinates only.

The resulting aggregate owner totals (covered/total and percentage) are:

| Owner | Lines | Branches | Functions |
| --- | ---: | ---: | ---: |
| common | 303/372 (81.45%) | 167/268 (62.31%) | 36/37 (97.30%) |
| bytecode | 1095/1217 (89.98%) | 648/860 (75.35%) | 75/78 (96.15%) |
| compiler | 1671/2105 (79.38%) | 1090/1759 (61.97%) | 120/129 (93.02%) |
| runtime | 2569/2952 (87.03%) | 1532/2301 (66.58%) | 245/256 (95.70%) |
| itemstore | 1866/2196 (84.97%) | 1203/1667 (72.17%) | 158/160 (98.75%) |
| libcall | 1339/1470 (91.09%) | 764/1021 (74.83%) | 121/123 (98.37%) |
| net | 861/1622 (53.08%) | 494/1041 (47.45%) | 74/98 (75.51%) |
| executable | 653/795 (82.14%) | 317/475 (66.74%) | 38/38 (100.00%) |
| **all modules** | **10357/12729 (81.37%)** | **6215/9392 (66.17%)** | **867/919 (94.34%)** |

Run the coverage-only baseline audit with:

```sh
python3 tests/baseline/audit_baseline.py
```
