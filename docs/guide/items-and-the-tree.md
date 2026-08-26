## The Item ##

The fundamental unit in Sinistra is the *item*. An item can contain integers,
floats, strings, Boolean values, `nil`, or code. The exact call, parameter,
return, and side-effect rules are normative in the [canonical language
reference](../reference/language.md#item-calls-and-code-item-execution). A
value item is not executed: an item expression returns a clone of its stored
value. A code item executes synchronously; a missing or invalid target returns
`nil`. All items therefore produce a value, even when that value is `nil`.

Items can contain various values, which are defined in the [canonical language
reference](../reference/language.md#values-and-operator-semantics). Of particular note is the concept of a "code" item. These are special items, in that rather than holding a value they execute Sinistra code.  They may optionally return a result.  For more information, see the documentation on [code items](code-items.md).

Items are nominally hierarchical, although this is only an organisational strategy – there is no inheritance.  Thus the following are items:  
`foo`  
`foo.bar`  
`foo.bar.baz`  
…although there is nothing can be inferred from `foo.bar` by its relationship with `foo`.

Item names are case-insensitive. `player.name`, `Player.Name`, and
`PLAYER.NAME` therefore refer to the same item; internally, item layer names
are normalised to lower case. The same rule applies to local variable names
and to Sinistra keywords.

Item layer names are strings. Integer layer literals and integer-valued
dereferences are converted to canonical base-10 integer text, so `foo.1` and
`foo.[@i]` with `@i = 1` address the same item. Float values are not permitted
as layer names or as dereference results used to build a layer name: a literal
such as `foo.1.0` is parsed as layers `foo`, `1`, and `0`, not as a float layer,
and a runtime dereference that evaluates to a float makes item assembly fail and
produce `nil`. Consequently `1`, `1.0`, and `1.00` do not have three
float-derived item-name spellings: only integer `1` is supported as a numeric
layer value, while the dotted forms are normal multi-layer string names if
written explicitly. Likewise `+0.0` and `-0.0` have no item-name mapping, and
NaN payloads are neither preserved nor normalized for item names because NaN
float values are rejected rather than formatted.

Item paths used by the runtime and itemstore APIs have at most eight non-root
layers. Each layer is 1 to 32 bytes and may contain only ASCII letters, digits,
and `_`; the complete non-root path is at most 263 bytes including dots. These
checks apply before mutation as well as to lookup and deletion. When an API is
given a non-root item as its starting pointer, that item's ancestor depth and
path are included in the same limits. Invalid paths are rejected without
partially creating a path. The v1 root-name exception remains: the root name
is limited to 32 bytes but is not restricted to the non-root character set.

To assign an item, use the assignment operator, `=`.  If the item does not exist, it will be created (as will all of its parents, if it is a multi-layered item).  If the item exists, its value will be overwritten with the new value.  An item which does not exist has the default value of `nil`.  Thus:  
`foo = 10;`  
`bar = 10 * foo;`  
(bar is now equal to 100)  
`bar = 10 * wibble`  
(wibble does not exist, and has the default value of `nil`.  `10 * nil` is `nil`, so the value of bar is also `nil`)

The examples above are examples of immediate execution.  Once executed, the result is given and the steps to create it are forgotten.  However, let’s instead make bar a code item.  Code items are evaluated each time they are called.

`bar = code ( return 10 * wibble; );`
Now, bar is equal to `nil`, but if we define  
`wibble = 7;`  
then bar will be equal to 70.  If we redefine  
`wibble = 3;`  
then bar will now return 30.

Code items can contain local variables.  There is only one scope: the item.  Thus, a local variable is visible from the moment it is defined to the end of the item.  Local variables are defined by assignment.

`dingdong = code ( @a = bar; @b = 100; return @a + @b; );`

When `dingdong` is executed, it assigns the value of `bar` to local variable `@a`, the value of `100` to local variable `@b`, adds `@a` and `@b` together, and returns the result. `return expression;` evaluates its expression once and immediately exits the current code item; `return;` exits with `nil`. Every expression statement is evaluated and discarded, including the final one. Falling off the end of a code item returns `nil`; no residual expression value becomes an implicit result.

When compiling code, if the parser doesn't like the source which it is chewing on, it will bail out and set `error` to an error number, and `error.msg` to the appropriate error message.  Thus an easy way to check if the code has compiled is to test these items.  A successful compilation will set these items to `nil`.

You can pass parameters to items, too. Arguments evaluate left-to-right before
the target is resolved. Parameters bind in declaration order; they are locals
private to the invocation. If you pass too many arguments, only the first N
distinct parameter slots are retained and the extras are ignored. Duplicate
parameter names reuse the same slot, ordered by first occurrence. If you pass
too few, trailing parameters receive `nil`. Here is an item which takes two
arguments:
```
add = code {@a, @b} ( return @a + @b; );
if error then
  sys.log{"Compilation failed:\n"};
  sys.log{error.msg};
  sys.log{"\n"};
endif;
```

If you call `add` with no arguments, you are effectively calling `add{nil, nil};`, and because `+` treats `nil` as integer `0`, the result is integer `0`. Calling `add{1};` is effectively `add{1, nil};` and returns `1`. Calling `add{1, 2, 3};` returns `3`, because the third argument is intentionally dropped. The same tolerant behavior applies when arguments are supplied to a missing item or to an expression that does not resolve to an item name; those calls return `nil`. Starting `sin` with `--strict-runtime-contracts` keeps executing with the same result behavior, but records `ERR_RUNTIME_INVALIDARGS` in `error`, writes a diagnostic in `error.msg`, and logs whenever a call has to discard arguments.

Strict dropped-argument diagnostics are therefore opt-in. For example,
`add{1, 2, 3};` still returns `3` in both modes, but strict mode also reports
that one extra argument was discarded. Likewise, `missing.item{1};` still
returns `nil`, but strict mode reports that the argument to the missing target
was dropped. Run without `--strict-runtime-contracts` for normal live-update
operation; run with it when you want these mismatches surfaced during testing
or development. A value item is not executed and returns a clone of its value,
regardless of supplied arguments.
