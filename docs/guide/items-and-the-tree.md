# Items and the Item Tree

## Paths and Layers
The fundamental unit in Sinistra is the item. An item's name consists of one or
more layers, which together form its path.

Items are nominally hierarchical, although this is only an organisational
strategy – there is no inheritance.  Thus the following are items:
```
foo
foo.bar
foo.bar.baz
```
…although there is nothing can be inferred from `foo.bar` by its relationship with `foo`.

Item names are case-insensitive. `player.name`, `Player.Name`, and
`PLAYER.NAME` therefore refer to the same item; internally, item layer names
are normalised to lower case. The same rule applies to local variable names
and to Sinistra keywords.

Each part of a path is called a layer. Layers may be written directly, as in
`players.42.name`, or constructed dynamically using dereferencing, which we
shall meet later.

Item paths may also be relative to the currently executing item's location by
using the leading-dot form. The precise resolution rules are described in the
Language Reference.  Thus:
```sinistra
add.first = 10;
add.second = 5;
add = code ( return .first + .second; );
/* equivalent to */
add = code ( return add.first + add.second; );
```

Sinistra places limits on path depth, layer length and the characters which may
appear in a layer. The exact limits are given in the
[Language Reference](../reference/language.md).

## Value Items
An item is either a value item or a code item. A value item may contain `nil`,
a Boolean, integer, float, string, list, or item reference. A code item
contains executable Sinistra bytecode.

A value item is not executed: an item expression returns a clone of its stored
value. A code item executes synchronously; a missing or invalid target returns
`nil`. All items therefore produce a value, even when that value is `nil`.

## Code Items
Of particular note is the concept of a "code" item. These are special items, in
that rather than holding a value they execute Sinistra code.  They may
optionally return a result, but if they do not they are deemed to return `nil`.

## Assignment and Replacement of Items
To assign an item, use the assignment operator, `=`.  If the item does not
exist, it will be created (as will all of its parents, if it is a multi-layered
item).  If the item exists, its value will be overwritten with the new value.
An item which does not exist produces `nil`.  Thus:
```sinistra
foo = 10;
bar = 10 * foo;
```
(bar is now equal to 100)
```sinistra
bar = 10 * wibble;
```
(`wibble` does not exist, and has the default value of `nil`.  `10 * nil` is
`nil`, so the value of bar is also `nil`)

The examples above are examples of immediate execution.  Once executed, the
result is given and the steps to create it are forgotten.  However, let’s
instead make bar a code item.  Code items are evaluated each time they are
called.

```sinistra
bar = code ( return 10 * wibble; );
```
At this point, evaluating `bar` returns `nil`, because `wibble` does not yet
exist. But if we now define `wibble`:
```sinistra
wibble = 7;
```
then bar will be equal to 70.  If we redefine
```sinistra
wibble = 3;
```
then bar will now return 30.

Code items can contain local variables.  There is only one scope: the item.
Thus, a local variable is visible from the moment it is defined to the end of
the item.  Local variables are defined by assignment.
```sinistra
dingdong = code ( @a = bar; @b = 100; return @a + @b; );
```
When `dingdong` is executed, it assigns the value of `bar` to local variable
`@a`, the value of `100` to local variable `@b`, adds `@a` and `@b` together,
and returns the result. `return expression;` evaluates its expression once and
immediately exits the current code item; `return;` exits with `nil`.
Every expression statement is evaluated and discarded, including the final one.
Falling off the end of a code item returns `nil`; no residual expression value
becomes an implicit result.

## Item References
When the bytecode interpreter encounters an item, it evaluates the item and
uses the result.  This is almost always what you need.  However, there are some
situations (for example constructing lists of items) where you need to refer
to the item itself, not its value.  For these situations, you need to use an
item reference.  The item reference operator is `&` prefixed to the item you
need to refer to.  Thus:
```sinistra
wibble = 42;
snap = wibble;
crackle = &wibble; /* sys.fetch{crackle} would return 42 */
```
In this example, `snap` contains the integer `42`, while `crackle` contains an
item reference whose path is `wibble`. If `wibble` is later changed to `"abc"`,
`snap` still contains `42` and `crackle` still contains the reference
`&wibble`. To fetch the value currently found at that path, use
`sys.fetch{crackle}`:
```sinistra
wibble = 42;
crackle = &wibble;
/* sys.fetch{crackle} would return 42 */
wibble = "abc";
/* sys.fetch{crackle} would return "abc" */
```
## Dynamic Items and Dereferencing
In any non-trivial world, it will sometimes be necessary to refer to an item
without knowing its exact path at compile time.  For this reason, item
dereferencing was invented.  The dereferencing operator is `[...]`:
```sinistra
@room = player.[input.line].room;
```
The dereference operator means "evaluate this and substitute its value into the
item path".  Both items and local variables can be dereferenced, and
dereferences may be nested.

The key distinction between `[...]` and `&` is that `[...]` is concerned with
item-path construction.  In other words:
- `&foo`: constructs an item reference value;
- `foo.[@bar]`: dynamically constructs an item path by substituting a layer.

## Calling Items
Code items may declare parameters:
```sinistra
add = code {@a, @b} (
  return @a + @b;
);
```
Arguments are supplied in braces:
```sinistra
result = add{2, 3};
```
Parameters are local to each invocation. If fewer arguments are supplied than
there are parameters, the remaining parameters receive nil. Extra arguments are
evaluated but ignored in normal operation.

A value item is not executed when called; it simply yields a clone of its
stored value. The complete call contract, including strict diagnostic
behaviour, is described in the [Language Reference](../reference/language.md).
