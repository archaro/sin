# Sinistra language reference (0.7.3)

This is the canonical reference for the implemented 0.7.2 Sinistra source
language as shipped with the 0.7.3 documentation. “Canonical” means that this
page is the normative description when other prose differs; it does not freeze
compatibility before 0.8.0. The reference is derived from
[`src/compiler/lexer.l`](../src/compiler/lexer.l),
[`src/compiler/parser.y`](../src/compiler/parser.y), lowering, and existing
compiler/runtime tests. Library APIs remain in [`libcalls.md`](libcalls.md),
the bytecode wire format in [`bytecode.md`](bytecode.md), and persistence in
[`itemstore-format.md`](itemstore-format.md).

## Lexical structure

Source is read as ASCII-oriented tokens. Keywords and layer/local names are
case-insensitive and normalized to lower case. Spaces, tabs, and newlines are
ignored between tokens. A `/* ... */` comment may contain any characters and
is ignored; an unterminated comment is an error. Comments are not accepted in
the lexer state between `code` and the opening `(` or `{` of a code value.

Token forms are:

- A local is `@[a-z_][a-z0-9_]*`.
- A layer is `[a-z0-9_]+`.
- An integer is `[0-9]+`; a float is
  `[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?`.
- A string is enclosed in double quotes. `\n`, `\t`, `\r`, `\b`, and `\f`
  produce their usual bytes; `\0nn` is an octal byte escape (`nn` are two
  octal digits), and a backslash followed by any other character quotes it.
  A newline or end-of-file before the closing quote is invalid. String
  payloads are limited by `SIN_MAX_STRING_BYTES`.

Reserved words are `and`, `break`, `code`, `continue`, `do`, `else`, `elsif`,
`endif`, `endwhile`, `if`, `nil`, `or`, `return`, `then`, `true`, `false`, and
`while`. Library prefixes `list`, `net`, `str`, `sys`, and `task` are recognized
for libcall syntax. Punctuation tokens are `=`, `==`, `!`, `!=`, `<`, `<=`,
`>`, `>=`, `++`, `+`, `--`, `-`, `*`, `%`, `/`, `(`, `)`, `,`, `{`, `}`, `;`,
`.`, `#[`, `&`, `[` and `]`. Any character that does not begin one of these
tokens is invalid input. Unterminated strings, code bodies, embedded quoted
strings, and comments are rejected.

## Concrete grammar

This EBNF is a readable rendering of the parser grammar. `ε` means empty;
quoted punctuation is a terminal.

```ebnf
program          ::= statement-list ;
statement-list   ::= ε | statement-list statement ";" ;
statement        ::= "while" expression "do" statement-list "endwhile"
                   | "do" statement-list "while" expression
                   | "if" expression "then" statement-list elsif-or-else "endif"
                   | "break" | "continue" | "return" | "return" expression
                   | local "=" expression | item "=" assignment-value
                   | local "++" | local "--" | expression ;
elsif-or-else    ::= ε
                   | "elsif" expression "then" statement-list elsif-or-else
                   | "else" statement-list ;
assignment-value ::= expression | "code" parameters code-body ;
expression       ::= local | integer | float | string | "true" | "false" | "nil"
                   | list | item-reference | item arguments
                   | expression "==" expression | expression "!=" expression
                   | expression "or" expression | expression "and" expression
                   | expression "<" expression | expression "<=" expression
                   | expression ">" expression | expression ">=" expression
                   | expression "+" expression | expression "-" expression
                   | expression "*" expression | expression "/" expression
                   | expression "%" expression | "(" expression ")"
                   | "!" expression | "-" expression | libcall ;
libcall          ::= library "." layer arguments ;
parameters       ::= ε | "{" parameter-list "}" ;
parameter-list   ::= local | local "," parameter-list ;
arguments        ::= ε | "{" argument-list "}" ;
argument-list    ::= expression | expression "," argument-list ;
list             ::= "#[" "]" | "#[" list-elements "]" ;
list-elements    ::= expression | expression "," list-elements ;
item-reference   ::= "&" item ;
item             ::= first-layer subsequent-layers
                   | "." first-layer subsequent-layers ;
first-layer      ::= layer | dereference ;
subsequent-layers ::= ε | "." layer subsequent-layers ;
layer            ::= identifier-layer | integer | dereference ;
dereference      ::= "[" (item | local) "]" ;
```

Here `local` is the local token, `library` is one of `list`, `net`, `str`,
`sys`, or `task`, and `identifier-layer` is a layer token. `code-body` is the
raw text between the balanced parentheses after `code`; nested parentheses
delimit the body, while quoted strings protect embedded parentheses. During
capture, tabs and newlines become spaces. Comment markers in this captured body
remain raw body text rather than starting comments in the outer lexing pass.
Every statement in a statement list ends with `;`. A code value is
an expression only in the item-assignment form shown above.

## Precedence and associativity

All binary operators are left-associative. From lowest to highest:

| Level | Operators | Associativity |
|---|---|---|
| 1 | `or` | left |
| 2 | `and` | left |
| 3 | `==`, `!=`, `<`, `>`, `<=`, `>=` | left |
| 4 | `+`, `-` | left |
| 5 | `*`, `/`, `%` | left |
| 6 | unary `!`, unary `-` | unary (right-nesting) |

Parentheses override these declarations. `++` and `--` are statement forms,
not expression operators.

## Evaluation order

- Binary operands are evaluated left operand first, then right operand.
- Arguments and list elements are evaluated left-to-right.
- `and` skips its right side when its left value is falsy; otherwise it
  evaluates the right side. `or` evaluates its right side only when its left
  value is falsy. Both return a normalized boolean.
- For `local = expression`, the value is evaluated before storing the local.
  For `item = expression`, the item path (including dereferences) is evaluated
  first, then the value, then the store.
- For an item call, argument expressions are evaluated in source order before
  the call target expression; the call then runs. Libcall arguments are also
  evaluated left-to-right before the fixed library call.
- Statements execute in source order. `return` evaluates its optional
  expression once and exits; `return;` returns `nil`. `break` and `continue`
  transfer to the nearest enclosing loop.

## Values and operator semantics

The runtime has exactly seven value types: `nil`, boolean, signed 64-bit
integer, IEEE-754 binary64 float, byte string, item reference, and list. The
tables below are normative for the implemented 0.7.2 language. Results of
comparisons and logical operators are always canonical booleans (`true` or
`false`). `I`, `F`, `S`, `N`, `B`, `R`, and `L` abbreviate integer, float,
string, nil, boolean, item reference, and list.

### Truthiness

| Type/value | Truth value |
|---|---|
| `nil` | false |
| boolean | its stored boolean value |
| integer | false only for `0` |
| float | false only for `+0.0` or `-0.0`; infinities and NaN are true |
| string | false only for the empty string |
| item reference | true, including an unresolved reference |
| list | false only for an empty list (a malformed/null list payload is false) |

`!x` first converts `x` using this table, then returns the opposite canonical
boolean. `and` and `or` short-circuit according to the evaluation-order rules
above and return canonical booleans rather than either operand.

### Equality and ordering

`==` and `!=` always produce booleans. Equality supports same-type pairs as
follows, plus numeric promotion: when either operand is `F`, an `I` operand is
converted to binary64 and the pair is compared as floats.

| Pair | `==` / `!=` |
|---|---|
| `I`/`I`, `F`/`F`, or `I`/`F` | numeric equality; `!=` is its negation |
| `B`/`B` | stored boolean equality |
| `N`/`N` | equal |
| `S`/`S` | byte-for-byte string equality |
| `R`/`R` | equal when canonical item paths are equal (not pointer identity) |
| `L`/`L` | recursive, element-by-element equality |
| any other pair | unequal; therefore `!=` is true |

IEEE-754 rules apply to float equality: NaN is unequal to every value,
including itself; `!=` is true for NaN. Positive and negative zero compare
equal, including to integer zero. Lists compare recursively using these same
rules; item references compare their stored canonical paths, even when they do
not resolve to an item.

Ordered comparisons support only the following pairs; every other or
mismatched pair yields `false` for all four operators.

| Supported pair | Ordering |
|---|---|
| `I`/`I`, `B`/`B` | signed integer/boolean ordering (`false < true`) |
| `I`/`F`, `F`/`I`, `F`/`F` | binary64 ordering after integer promotion |

Any ordered comparison with NaN is `false`, including `NaN <= NaN` and
`NaN >= NaN`.

### Implicit coercion and arithmetic

There is no general string/numeric/boolean coercion. Integer/float mixing is
the only numeric promotion, and it produces a float result. The sole additional
coercion is for `+`: `nil` is treated as integer zero when paired with `nil` or
an integer. `nil` is not numeric for any other operator, and `nil + float` is
invalid. String concatenation is available only for `S + S`.

The following matrix gives the exact result contract. “invalid” means the
operator's result shown in the final column, not a generic conversion rule.

| Operator | Valid operand pairs and result | Invalid operands/result |
|---|---|---|
| `+` | `I+I`, `N+I`, `I+N`, `N+N` -> `I`; any `I`/`F` or `F`/`F` -> `F`; `S+S` -> concatenated `S` | all other pairs -> `nil` |
| `-` | `I-I` -> `I`; any `I`/`F` or `F`/`F` -> `F` | all other pairs -> `nil` |
| `*` | `I*I` -> `I`; any `I`/`F` or `F`/`F` -> `F` | all other pairs -> `nil` |
| `/` | `I/I` -> `I` (`x/0` is integer `0`); any `I`/`F` or `F`/`F` -> `F` with IEEE-754 division | invalid pair with no `F` operand -> integer `0`; invalid pair with an `F` operand -> `nil` (VM compatibility split) |
| `%` | `I%I` -> `I` (zero divisor -> `nil`); any `I`/`F` or `F`/`F` -> `F` using `fmod` | all other pairs -> `nil` |
| unary `-` | integer or float -> same type, negated | other types are left unchanged (the VM reports failure but keeps the value) |

Integer `+`, `-`, `*`, `/`, and unary `-` detect signed 64-bit overflow and
return `nil`; `INT64_MIN / -1` is therefore `nil`. Integer remainder truncates
toward zero, with the non-zero result taking the left operand's sign;
`INT64_MIN % -1` is integer zero. Floating arithmetic follows binary64,
including infinities and NaN; floating `%` is C `fmod`, so a zero divisor
produces NaN.
