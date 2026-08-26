# Lists and Item References

The full details of list and item-reference semantics are to be found in the
[language reference](../reference/language.md#lists-and-item-references). This
document serves as a friendly introduction.

## Lists

Lists are constructed using #[]. Elements are evaluated exactly once,
left-to-right; `#[fred]` stores the result of evaluating `fred`, while
`#[&fred]` stores an item reference. Empty, singleton, nested, and
expression-position lists are valid; trailing commas are rejected.

The following are all examples of how a list may be constructed:
- `foo = #[]; /* an empty list */`
- `foo = #["purple", "monkey", "dishwasher"]; /* a list containing three strings */`
- `foo = #[snap, "crackle", &pop]; /* contains the return value of snap, the string "crackle", and a reference (unevaluated) to pop */`
- `foo = #[ #["bar", "baz"], #[1.5, 2.78, 3.14] ]; /* contains two lists */`

Lists are immutable: operations which derive a changed list return a new list
rather than modifying the original. Lists may contain any Sinistra value,
including other lists and item references.
```sinistra
@a = #[1, 2, 3];
@b = list.append{@a, 4};
```
In this example, `@a` remains `#[1, 2, 3]`, while `@b` contains `#[1, 2, 3, 4]`.
## Iteration

`FOREACH @local IN expression DO ... ENDFOR` is a statement that visits each
element of a list in order. The expression is evaluated exactly once before the
iterator is assigned or the body is entered; a code item there is therefore
called once.  See the documentation on [control flow](control-flow.md)
for more information.

## Item references

An item reference stores an item path rather than the item's current value. It
may remain valid as a value even when its target does not presently exist, and
resolves the path afresh when used.  See the documentation on [items and the
item tree](items-and-the-tree.md) for more information.

Item-reference calls are
- `sys.itemref{"fred"}`
- `sys.itemname{@ref}`
- `sys.fetch{@ref}`
- `sys.call{@ref,@arguments}`
Full information about them is to be found in the
[`sys` libcall documentation](../reference/libcalls-sys.md)
