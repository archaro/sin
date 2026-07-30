# Lists and item references (planned / in development)

This feature is planned and its syntax is not implemented. The following
contracts are frozen before any representation-dependent code is introduced.

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

Planned calls are `sys.itemref{"fred"}` (string to reference or nil),
`sys.itemname{@ref}`, `sys.fetch{@ref}`, and
`sys.call{@ref,@arguments}`. Existing name-taking sys calls (including exists,
itemtype, childcount, paramcount, source, and delete) will share a resolver and
accept strings or references where sensible. Strings remain strings.
