# 0.8.0 Performance Evidence

This is the checked-in evidence record for the 0.8.0 performance benchmark.
The measurements are local, release-build timings from the exact tree recorded
below; they are not portable timing budgets.

## Benchmark Configuration

The supported benchmark target completed successfully (`1/1` descriptor):

```text
make bench
```

The detailed measurements were captured from the same release benchmark with
`TF_VERBOSE=1 SIN_EXTENDED_BENCH=1 SIN_BENCH_REPORT=1`. The run used GCC
13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) on Linux 6.8.0-138-generic,
x86_64 on 2026-08-29, at commit `73e3ec5` (`Raise and document itemstore
limits`). The benchmark's strict elapsed-time thresholds were disabled. Sample
counts are shown in each table; the persistent-list, itemstore, item-reference,
and `sys.call` rows use three samples, while network, string-registry, and
runtime verification rows use five.

## Core Operations

| Workload | Samples/iterations | Median |
| --- | ---: | ---: |
| `libcall_lookup_pair(str.upper)` | 150,000 iterations | 2,088,278 ns total (13 ns/op) |
| `libcall_func_pair(str.upper)` | 3,000,000 iterations | 15,877,015 ns total (5 ns/op) |
| Value clone/move/replace/free | 5 × 100,000 | 2,746,413 ns (27 ns/op) |
| Stack push/pop, scalar | 5 × 100,000 | 535,384 ns (5 ns/op) |
| Stack push/pop, string | 5 × 100,000 | 2,877,290 ns (28 ns/op) |
| Item value fetch/assignment | 5 × 10,000 | 2,957,125 ns (295 ns/op) |
| Interpreter control path | 5 × 10,000 | 10,033,076 ns |
| Interpreter argument/return path | 5 × 10,000 | 10,855,843 ns (1.082× control) |

## Network Input

| Workload | Records | Input bytes | Maintenance bytes | Input-buffer allocations | Median |
| --- | ---: | ---: | ---: | ---: | ---: |
| short-16k | 8192 | 16384 | 0 | 2 | 219892 ns |
| short-64k | 32767 | 65534 | 0 | 2 | 1001249 ns |
| mixed-partial-complete-long | 6137 | 16382 | 0 | 1 | 180902 ns |

## Persistent Lists

| Operation | Shape/size | Median |
| --- | --- | ---: |
| Construct | 0 | 3460 ns (43 ns/invocation) |
| Construct | 8 | 13230 ns (165 ns/invocation) |
| Construct | 1024 | 479334 ns (5991 ns/invocation) |
| Clone/release | 0, 8, 1024 | 370, 340, 340 ns (4 ns/invocation) |
| Random get | 8; 1024 | 2380; 3430 ns (3; 5 ns/access) |
| Sequential get | 8; 1024 | 1320; 946078 ns (2; 11 ns/access) |
| Set | 8; 1024 | 9750; 39370 ns (121; 492 ns/invocation) |
| Concat | 8; 1024 | 15130; 725117 ns (189; 9063 ns/invocation) |
| Slice | 8; 1024 | 11750; 51700 ns (146; 646 ns/invocation) |
| Unaligned concat | 31+1025 (reported 1056) | 1349152 ns (16864 ns/invocation) |
| Aligned concat | 32+1024 (reported 1056) | 481804 ns (6022 ns/invocation) |
| Unaligned concat | 1023+1025 (reported 2048) | 1461952 ns (18274 ns/invocation) |
| Aligned concat | 1024+1024 (reported 2048) | 742467 ns (9280 ns/invocation) |
| Append | 31→32; 32→33; 1055→1056; 1056→1057 | 21050; 8050; 19521; 16500 ns (263; 100; 244; 206 ns/invocation) |
| Equal | 1024 elements | 279862 ns (3498 ns/invocation) |
| Early unequal | 1024 elements | 1650 ns (20 ns/invocation) |
| Late unequal | 1024 elements | 355523 ns (4444 ns/invocation) |
| Compiled source list literal | 33 elements | 168392 ns (2104 ns/invocation) |
| Aligned shared slice | 32/992 from 1056 | 92110 ns (1151 ns/invocation) |
| Aligned subtree slice | 1024/1056 from 2080 | 3430 ns (42 ns/invocation) |
| Aligned short-tail slice | 65 elements | 2400 ns (30 ns/invocation) |
| Unaligned boundary slice | 1056 elements | 37060 ns (463 ns/invocation) |

## String Registry

Capacity lookup remained constant at approximately one probe node per
operation across all tested live populations. The five-sample medians were:

| Live strings | Capacity lookup | Removal | Lookup probe nodes/op | Removal probe nodes/op |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 320 ns (4 ns/op) | 50 ns (50 ns/op) | 1.0 | 1.0 |
| 32 | 300 ns (3 ns/op) | 1360 ns (42 ns/op) | 1.0 | 1.2 |
| 1024 | 310 ns (3 ns/op) | 430 ns (5 ns/op) | 1.0 | 1.0 |
| 4096 | 330 ns (4 ns/op) | 500 ns (6 ns/op) | 1.0 | 1.0 |

Reuse-concat versus growth-concat ratios were 3.167, 1.935, 2.243, and
2.223 at populations 1, 32, 1024, and 4096 respectively. Interpreter concat
medians were 82241, 85961, 87471, and 101160 ns (1028, 1074, 1093, and
1264 ns/op) at those populations.

## Itemstore, Item References, and Runtime Verification

| Workload | Median |
| --- | ---: |
| Itemstore v2 save (list of 33; 3 samples) | 1,739,145 ns |
| Itemstore v2 load (list of 33; 3 samples) | 16,820 ns |
| Item reference creation (3 samples) | 50,270 ns (50 ns/op) |
| Existing item-reference path resolution (3 samples) | 100,251 ns (100 ns/op) |
| `sys.call` with list of 8 arguments (3 samples) | 185,422 ns (2317 ns/invocation) |
| `sys.call` with zero arguments (3 samples) | 142,871 ns (1785 ns/invocation) |

Runtime verification cache timings (five-sample medians) were 1207 ns/op for
repeated calls, 1059 ns/op for alternating calls, 2598 ns/op for replacement
calls, and 1872 ns/op for a forced-cold same-path call. The cold/repeated ratio
was 1.552.

## Evidence Policy

Elapsed measurements are local evidence from this machine and must not be read
as cross-machine performance guarantees. Probe counts, input maintenance and
allocation counters, and benchmark pass/fail status are the durable regression
signals; elapsed-time thresholds remain opt-in. The final benchmark descriptor
passed (`TF|TOTAL|selected|1|1|0`).
