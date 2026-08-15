## Values and operators ##

The complete value model, truthiness rules, equality/ordering matrices, and
arithmetic result contracts are maintained in the [canonical language
reference](../reference/language.md#values-and-operator-semantics). The summary
here is intentionally introductory: there are seven runtime value types, and
`+`, `-`, `*`, `/`, `%`, comparisons, `!`, `and`, and `or` do not perform
general string/numeric/boolean coercion. In particular, `+` has the documented
`nil`-as-zero special case. Invalid `/` has a compatibility split: a pair with
no float operand produces integer zero, while an invalid pair containing a
float produces `nil`. Lists are interesting, too. See the documentation on [lists](lists.md) for more information.

Arithmetic operators have the same precedence and left associativity as shown
in the canonical reference. The unary postfix operators `++` and `--` operate
on local variables but not items, and are statements rather than expressions.
Thus the following is invalid:

`WHILE @a++ < 100 DO ...; ENDWHILE;`

The usual boolean comparison operators are present; `||` and `&&` are not.
Use `or` and `and`, which short-circuit and return normalized booleans.

Boolean literals are `true` and `false`; `nil` is the explicit nil literal (all
three are case-insensitive reserved words).

Truthiness includes non-empty lists and all item references (resolved or not);
empty lists are false. See the canonical truthiness table for float NaN,
signed-zero, and malformed aggregate edge cases.

Examples:
`is_wizard = true;`
`is_guest = false;`
`if is_wizard == true then ...; endif;`
`if is_guest == false then ...; endif;`

Strings may be concatenated with `+` only when both operands are strings.

The usual operator precedence applies, and (parentheses) can be used to change this.
