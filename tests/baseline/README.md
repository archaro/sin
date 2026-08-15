# Pre-rewrite baseline

This directory is the authoritative, checked-in baseline for the test-harness
rewrite. The old gate remains authoritative until cutover; no legacy row may
be removed or silently consolidated before parity is recorded here.

Files:

- [`legacy_test_ledger.csv`](legacy_test_ledger.csv) — all 379 legacy checks:
  351 unified-harness registrations, 19 standalone network registrations, one
  chat-smoke check, and eight logical output-contract checks.
- [`coverage_snapshot.csv`](coverage_snapshot.csv) — GCC/gcov counts and
  percentages for every authored production `src/**/*.c` module.
- [`audit_baseline.py`](audit_baseline.py) — deterministic validation of row
  counts, required fields, owners/categories, source-module coverage rows, and
  count/percentage consistency. It reads only checked-in files and source
  paths; it does not require build artifacts.

## Recorded provenance

- Baseline commit: `ae4b9b92283e7e6ac3db61541eb65617db53dac7`
- Baseline date: `2026-08-14` (UTC)
- Compiler: `gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`
- Coverage tool: `gcov (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`

The exact clean and instrumented full-gate command was:

```sh
make clean && make test \
  CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -fprofile-arcs -ftest-coverage -O0 -g' \
  LDFLAGS='-fprofile-arcs -ftest-coverage'
```

It completed with exit 0 and
`[test] totals: ran=379 passed=379 failed=0 skipped=0 status=SUCCESS [PASS]`.
The coverage collection command, run after that gate, was the following
per-module invocation (with `<module-dir>` and `<module>.c` expanded for every
authored C source):

```sh
gcov -b -f --json-format -o obj/debug-gcc/<module-dir> src/<module>.c
```

The standalone network translation unit was collected separately with:

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

## Parity rule

Every ledger row must remain accounted for. The current plan is one-for-one:
each row has a deterministic `planned_replacement_id`. A future intentional
consolidation must change every affected row's parity note from one-for-one,
name the equivalent replacement ID, and explain why every old behavior and
assertion remains covered before any old row or test is removed.

Run the audit with:

```sh
python3 tests/baseline/audit_baseline.py
```
