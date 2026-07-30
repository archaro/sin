# Lists and item references

List libcalls remain out of scope. Phase 3 provides the
immutable C runtime value described below; item references remain available
through their existing internal API.

Phase 4 adds parser and AST support for list literals and item-reference
syntax. Lists and references are not executable yet; lowering, bytecode,
runtime operations, persistence, and source-like rendering remain deferred.

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
`sin_list_get()` borrows an element, while `sin_list_append()` and
`sin_list_set()` borrow their inputs and return a new owned list. List handles
and tree nodes are non-atomic reference counted. Count is limited to
1,048,576 elements and nesting depth to 64; an empty or scalar-only list has
depth 1. Cloning a `VALUE_list` shares its handle, and releasing a value
releases that handle. List plain-text rendering is deferred; debug output is a
bounded `<list:COUNT>` summary.

Initial API: `list.length{@list}`, `list.get{@list,@index}`,
`list.append{@list,@value}`, `list.set{@list,@index,@value}`,
`list.concat{@left,@right}`, and `list.slice{@list,@start,@length}`. Indices
are zero-based; negative or out-of-range indices return `nil`. Invalid argument
types also return `nil` and follow the existing strict-runtime-contract
reporting policy. `append`, `set`, `concat`, and `slice` return new lists. No
mutable push/pop/insert/remove or extra indexing syntax is planned initially.

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

Planned calls are `sys.itemref{"fred"}` (string to reference or nil),
`sys.itemname{@ref}`, `sys.fetch{@ref}`, and
`sys.call{@ref,@arguments}`. Existing name-taking sys calls (including exists,
itemtype, childcount, paramcount, source, and delete) will share a resolver and
accept strings or references where sensible. Strings remain strings.
