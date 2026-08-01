# Lists and item references

The immutable list runtime and public list libcalls are available; item
references remain available through their existing internal API.

List literals and item-reference syntax compile and execute.

## Lists

`#[` is one token (`# [` is invalid). Elements are evaluated exactly once,
left-to-right; `#[fred]` stores fred's value while `#[&fred]` stores a
reference. Empty, singleton, nested, and expression-position lists are valid;
trailing commas are rejected initially.

Lists are immutable shared values. Assignment, arguments, returns, item
storage, and cloning share storage; append/replacement operations return new
lists and aliases never mutate. Lists may contain any value, cannot be cyclic,
are false when empty and true otherwise, and use recursive structural `==` /
`!=`. Relational comparisons are unsupported and produce the normal invalid
comparison result. Identity is not exposed.

The C API in `src/runtime/list.h` stores a 32-way persistent vector with a
separate one-to-32-element tail. `sin_list_build_owned()` consumes every
owned input element (including failed builds) and clears each array slot;
`sin_list_get()` borrows an element, while `sin_list_append()`, `sin_list_set()`,
`sin_list_concat()`, and `sin_list_slice()` borrow their inputs and return a new
owned list. List handles
and tree nodes are non-atomic reference counted. Count is limited to
1,048,576 elements and nesting depth to 64; an empty or scalar-only list has
depth 1. Cloning a `VALUE_list` shares its handle, and releasing a value
releases that handle. List plain-text rendering is deferred; debug output is a
bounded `<list:COUNT>` summary.

Public API: `list.length{@list}`, `list.get{@list,@index}`,
`list.append{@list,@value}`, `list.set{@list,@index,@value}`,
`list.concat{@left,@right}`, and `list.slice{@list,@start,@length}`. Indices
are zero-based; negative or out-of-range indices return `nil` without changing
the error item. Wrong argument types return `nil` and report
`ERR_RUNTIME_INVALIDARGS` with current-item provenance. Construction or limit
failures return `nil` without changing the error item. `append`, `set`, `concat`,
and `slice` return new lists. No
mutable push/pop/insert/remove or extra indexing syntax is planned initially.

### Performance measurements

The representative list/item-reference matrix is opt-in: run `make
test-benchmark` (or set `SIN_EXTENDED_BENCH=1 SIN_BENCH_REPORT=1` when running
the optimized test binary). It reports representative medians: construct and
clone/release at 0, 8, and 1024; random and sequential get, set, concat, and
slice at 8 and 1024; append at 31→32, 32→33, 1055→1056, and 1056→1057;
equal/early-unequal/late-unequal at 1024; a runtime `BUILD_LIST` literal of 33
elements; itemstore v2 save/load; item-reference creation/resolution; and a
list/element transfer proxy. Results are machine-dependent; compare medians
and ratios rather than absolute budgets. Investigate a repeatable regression of
3% or more across repeated optimized runs. Normal `make test` does not run or
enforce the matrix.

## Item references

`&fred` and dynamic paths such as `&players.[@index]` produce immutable,
canonical root-relative paths. References are weak (not raw pointers), remain
valid values when dangling, resolve afresh on use, compare by canonical path,
and are truthy even when unresolved. Deletion and recreation of the same path
therefore preserves resolution behavior. References are assignable and planned
for itemstore v2 persistence.

Executable list literals evaluate elements left-to-right. Prefixing an item
expression with `&` builds an owning canonical item reference without fetching
the target.

Item-reference calls are `sys.itemref{"fred"}` (string to reference or nil),
`sys.itemname{@ref}`, `sys.fetch{@ref}`, and
`sys.call{@ref,@arguments}`. Existing name-taking sys calls (including exists,
itemtype, childcount, paramcount, source, and delete) share a resolver and
accept strings or references where sensible. Strings remain strings.
