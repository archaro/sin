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

### Performance measurements

The representative list/item-reference matrix is opt-in: run `make
test-benchmark` (or set `SIN_EXTENDED_BENCH=1 SIN_BENCH_REPORT=1` when running
the optimized test binary). It reports representative medians: construct and
clone/release at 0, 8, and 1024; random and sequential get, set, concat, and
slice at 8 and 1024; append at 31→32, 32→33, 1055→1056, and 1056→1057;
equal/early-unequal/late-unequal at 1024; a compiled source literal returning
33 elements; itemstore v2 save/load; item-reference creation/resolution; and
actual `sys.call` execution comparing an 8-element list with a zero-argument
control call. Results are machine-dependent; compare medians
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
