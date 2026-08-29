# Pre-0.8.0 Performance Evidence

This is the checked-in evidence record for the pre-0.8.0 performance work.
Historical paired measurements below were recorded while Tasks 5–8 were being
implemented. They are separate from the final benchmark run on the exact
working tree described later.

## Historical Paired Measurements

These results are same-machine evidence from repeated release-build samples;
they are not portable timing budgets.

| Task | Measurement | Result |
| --- | --- | --- |
| 5 | Paired nine-run medians | Aligned 1024+1024 concatenation improved 70.10%; append, set, and clone/release rows were within 3% or improved. |
| 6 | Three paired nine-sample rounds | The representative 1024-element slice improved about 93.5%; no repeatable unrelated regression exceeded 3%. |
| 7 | Linked-list string-registry scaling | The scaling measurement justified proceeding to Task 8. |
| 8 | Deterministic hash probes | Capacity lookup probes remained approximately constant from 1 through 4096 live strings. |

## Final Optimized Run

The final exact-tree run used GCC 13.3.0 on Linux 6.8.0-137-generic,
x86_64. The relevant current results below use five-sample medians.

### Network Input

| Workload | Records | Input bytes | Maintenance bytes | Input-buffer allocations | Median |
| --- | ---: | ---: | ---: | ---: | ---: |
| short-16k | 8192 | 16384 | 0 | 2 | 226122 ns |
| short-64k | 32767 | 65534 | 0 | 2 | 966169 ns |
| mixed | 6137 | 16382 | 0 | 1 | 182621 ns |

### Runtime Verification

The measurements use the actual fetch/transfer path: repeated calls were
1163 ns/op, alternating calls 1038 ns/op, replacement calls 2008 ns/op, and a
forced-cold same-path run 1844 ns/op. The cold/repeated ratio was 1.585.
Deterministic invocation counts over 400 iterations were 2 repeated, 4
alternating, and 800 for replacement/cold.

### Persistent Lists

| Workload | Median |
| --- | ---: |
| Aligned 1024+1024 concat (reported size 2048) | 9415 ns/invocation |
| Unaligned 1023+1025 concat | 19566 ns/invocation |
| Aligned shared 32/992 slice | 1096 ns/invocation |
| Aligned subtree 1024/1056 slice | 44 ns/invocation |
| Unaligned boundary slice | 638 ns/invocation |

### String Registry

Capacity-lookup `probe_nodes/op` was 1.0 at live populations 1, 32, 1024,
and 4096. Removal `probe_nodes/op` was respectively 1.0, 1.1, 1.0, and 1.0
at those populations.

## Evidence Policy

Elapsed measurements are local evidence from this machine and must not be
read as cross-machine performance guarantees. Deterministic copy, probe,
verifier, and clone counters are the durable regression policy; elapsed-time
thresholds remain opt-in. The final benchmark harness passed 351/351.
