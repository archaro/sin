# Lists and item references

Observable list and item-reference semantics are normative in the
[language reference](language-reference.md#lists-and-item-references). This
page records implementation/API notes only.

List literals and item-reference syntax compile and execute.

## Lists

`#[` is one token (`# [` is invalid). Elements are evaluated exactly once,
left-to-right; `#[fred]` stores fred's value while `#[&fred]` stores a
reference. Empty, singleton, nested, and expression-position lists are valid;
trailing commas are rejected initially.

Lists are immutable values. Assignment, arguments, returns, item storage, and
cloning preserve the same logical value; an implementation may share internal
storage, but that sharing is unobservable. Append/replacement operations return
new lists and aliases never mutate. Lists may contain any value, cannot be cyclic,
are false when empty and true otherwise, and use recursive structural `==` /
`!=`. Relational comparisons are unsupported and produce the normal invalid
comparison result. Identity is not exposed.

The C API in `src/runtime/list.h` stores a 32-way persistent vector with a
separate one-to-32-element tail. `sin_list_build_owned()` consumes owned input
elements only after a valid count and input reach the owned-build path; a
nonzero count with `NULL` input and an over-limit count are rejected before any
input is consumed. On an accepted build, each owned input is consumed and its
array slot cleared;
`sin_list_get()` borrows an element, while the update helpers borrow their
inputs and return a new owned list. Handles and tree nodes are non-atomic
reference counted; these ownership details do not alter the value semantics
above. `value_debug_string` renders source-like list text, with truncation and
error markers where required.

The public list API, argument validation, and failure behavior are documented
in [`libcalls.md`](libcalls.md). Indices are zero-based and update calls return
new lists; there is no mutable push/pop/insert/remove syntax.

### Packed layout and structural sharing

The persistent vector keeps complete 32-value logical leaves in its root and
one to 32 values in a separate tail. A concatenation whose left count is
32-aligned can retain complete right-hand leaves without cloning their values.
When that offset is unaligned, every subsequent destination leaf boundary is
shifted, so retaining a source leaf would violate the packed invariant; the
right-hand values are cloned once through the internal leaf cursor, in
canonical batches, in addition to cloning any incomplete left boundary needed
to form the result.

A full-range slice retains the source list. An aligned slice retains covered
complete leaves/subtrees and clones only a partial tail boundary. An unaligned
slice clones its selected values into canonical output leaves because its
source and destination leaf boundaries differ. Focused tests use deterministic
cursor-clone counters as complexity guards: aligned 32/992 slicing and large
aligned concatenation clone zero cursor values, while 31 + 1025 concatenation
and start=31,length=992 slicing clone exactly the values that must be
repacked.

## Iteration

`FOREACH @local IN expression DO ... ENDFOR` is a statement that visits each
element of a list in order. The expression is evaluated exactly once before the
iterator is assigned or the body is entered; a code item there is therefore
called once. The iterator is initialized to `nil`, is assigned each visited
element, and remains in scope after `ENDFOR`, holding the last visited element
or `nil` when there were none. Non-list values perform zero iterations without
changing `error`. The captured list value is immutable, so rebinding the source
local in the body does not affect the remaining iterations. `BREAK` exits the
nearest loop and `CONTINUE` advances to the next element. Each nested loop uses
three hidden locals and counts against the 255-local limit; sequential loops
reuse those hidden slots. See the language reference for the complete grammar
and scope rules.

### Performance measurements

The representative list/item-reference matrix is opt-in: run `make
test-benchmark` (or set `SIN_EXTENDED_BENCH=1 SIN_BENCH_REPORT=1` when running
the optimized test binary). It reports representative medians: construct and
clone/release at 0, 8, and 1024; random and sequential get, set, concat, and
slice at 8 and 1024; explicit slice shapes for aligned leaf sharing (1056,
32, 992), an aligned subtree (2080, 1024, 1056), an aligned short tail (65,
64, 1), and an unaligned 31/32/33 boundary (1056, 31, 33); append at 31→32,
32→33, 1055→1056, and 1056→1057;
equal/early-unequal/late-unequal at 1024; a compiled source literal returning
33 elements; itemstore v2 save/load; item-reference creation/resolution; and
actual `sys.call` execution comparing an 8-element list with a zero-argument
control call. It also measures the runtime string registry at 1, 32, 1024,
and 4096 live buffers, separating lookup/removal hash-probe work from reuse
and growth concatenation copying, cleanup, and an interpreted concat workload.
The Task 7 linked-list rows are retained as the before baseline; the current
pointer-keyed open-addressing registry should keep probe growth approximately
constant across those populations.
Results are machine-dependent; compare medians
and ratios rather than absolute budgets. Investigate a repeatable regression of
3% or more across repeated optimized runs. Normal `make test` does not run or
enforce the matrix.

## Item references

`&fred` and dynamic paths such as `&players.[@index]` produce immutable,
canonical root-relative paths. References are weak (not raw pointers), remain
valid values when dangling, resolve afresh on use, compare by canonical path,
and are truthy even when unresolved. Deletion and recreation of the same path
therefore preserves resolution behavior. References are assignable and are
persisted by itemstore v2 as canonical paths.

Executable list literals evaluate elements left-to-right. Prefixing an item
expression with `&` builds an owning canonical item reference without fetching
the target.

Item-reference calls are `sys.itemref{"fred"}` (string to reference or nil),
`sys.itemname{@ref}`, `sys.fetch{@ref}`, and
`sys.call{@ref,@arguments}`. Existing name-taking sys calls (including exists,
itemtype, childcount, paramcount, source, and delete) share a resolver and
accept strings or references where sensible. Strings remain strings.
