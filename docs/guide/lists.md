# Lists and item references

The full details of list and item-reference semantics are to be found in the
[language reference](../reference/language.md#lists-and-item-references). This
document serves as a friendly introduction.

## Lists

Lists are constructed using #[]. Elements are evaluated exactly once,
left-to-right; `#[fred]` stores fred's value while `#[&fred]` stores a
reference. Empty, singleton, nested, and expression-position lists are valid;
trailing commas are rejected.

Lists are immutable values. Assignment, arguments, returns, item storage, and
cloning preserve the same logical value. Append/replacement operations return
new lists and aliases never mutate. Lists may contain any value, cannot be
cyclic, are false when empty and true otherwise, and use recursive structural
`==` / `!=`. Relational comparisons are unsupported and produce the normal
invalid comparison result. Identity is not exposed.

The following are all examples of how a list may be constructed:
- `foo = #[]; /* an empty list */`
- `foo = #["purple", "monkey, "dishwasher"]; /* a list containing three strings */`
- `foo = #[snap, "crackle", &pop]; /* contains the return value of snap, the string "crackle", and a reference (unevaluated) to pop */`
- `foo = #[ #["bar", "baz"], #[1.5, 2.78, 3.14] ]; /* contains two lists */`

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
