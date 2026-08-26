# Expressions and Values

The complete value model, truthiness rules, equality/ordering matrices, and
arithmetic result contracts are maintained in the [canonical language
reference](../reference/language.md#values-and-operator-semantics).

## Operators and Values
Sinistra has seven runtime value types: `nil`, Boolean, integer, float, string,
item reference, and list. Operators have type-specific behaviour rather than
performing general coercion between strings, numbers and Booleans.

The arithmetic operators are `+`, `-`, `*`, `/`, and `%`. Comparisons use
`==`, `!=`, `<`, `<=`, `>`, and `>=`. Boolean logic uses `and`, `or`, and `!`.

One particularly useful Sinistra peculiarity is that `+` treats `nil` as
integer zero.

Lists are interesting, too. See the documentation on [lists](lists.md) for more information.

Operators follow the precedence rules given in the
[canonical language reference](../reference/language.md#precedence-and-associativity);
parentheses may be used to override them.

## Truthiness
`nil`, `false`, integer zero, floating-point zero, the empty string, and the
empty list are false. Non-zero numbers, non-empty strings and non-empty lists
are true. Item references are true whether or not they currently resolve to an
existing item.

A full discussion of truthiness will be found in the
[canonical language reference](../reference/language.md#truthiness).

Examples:
```sinistra
is_wizard = true;
is_guest = false;
if is_wizard == true then ...; endif;
if is_guest == false then ...; endif;
```

## Arithmetic and concatenation
Binary arithmetic operators have the same precedence and left associativity as
shown in the canonical reference.

Strings may be concatenated with `+` only when both operands are strings.

## Boolean Logic
Boolean literals are `true` and `false`; `nil` is the explicit nil literal.

The Boolean operators `and` and `or` short-circuit and return Boolean results.

## Increment and Decrement operators
The unary postfix operators `++` and `--` operate
on local variables but not items, and are statements rather than expressions.
Thus the following is invalid:

`WHILE @a++ < 100 DO ...; ENDWHILE;`

