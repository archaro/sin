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

## Item calls and code-item execution

An item expression with an argument block evaluates each argument from left to
right, then evaluates the target expression, and then performs the call. Each
argument and the target expression is evaluated once. The target is resolved
relative to the executing item using the ordinary item-name rules.

- A **code item** runs synchronously. The caller resumes at the next statement
  only after the callee has terminated by `RETURN`, `RETURN expression`, or
  ordinary fallthrough. A callee's locals and parameters are private to that
  invocation; assignments to items and completed libcall/itemstore effects are
  not rolled back when the result is discarded.
- A **value item** is not executed. The call expression produces a clone of the
  stored value (including `nil`, lists, strings, and references). Supplied
  arguments do not change that value.
- A missing item, an invalid computed item name, or a target whose name has an
  invalid value produces `nil`. It is not an execution failure at the language
  level.

For a code item, arguments bind to parameters in declaration order. Parameters
are locals for that invocation and do not share storage with a caller's locals.
The call contract is deliberately tolerant: with fewer arguments, trailing
parameters receive `nil`; with exactly the declared count, all arguments bind;
with more arguments, only the first N (the distinct declared parameter-slot
count) are kept and the rest are discarded. A parameter declaration names a
local; duplicate names refer to the same local and do not consume another
argument slot. Parameter slots are ordered by each name's first occurrence:
`code {@a, @a, @b} (...)` has two slots, binding argument 1 to `@a` and
argument 2 to `@b`. The compiler's established encoded limits apply: the
distinct local/parameter table and emitted parameter count are each limited to
255 entries. No stronger limit rule is part of this reference.

In default mode, discarded arguments for excess-argument, missing-target, and
invalid-target calls are silent and the call keeps its normal value (`nil` for
the latter two). With `--strict-runtime-contracts`, the same arguments are
discarded and the same values are returned, but the runtime also records
`ERR_RUNTIME_INVALIDARGS`, writes a diagnostic to `error.msg`, and logs the
contract violation. Strict mode changes diagnostics, not stack/result
semantics.

`RETURN expression;` evaluates its expression once, exposes that value as the
code item's result, and immediately transfers control to the caller. Bare
`RETURN;` and falling off the end both return `nil`. Every expression statement
is evaluated for its effects and discarded; a residual value on the execution
stack never becomes an implicit result. Statements after a taken return do not
execute. `BREAK` and `CONTINUE` affect only the nearest enclosing loop; a return
from inside a loop exits the whole current code item, while a break/continue
continues with that loop's normal control flow.

Argument expressions, target expressions, return expressions, and any side
effects they perform obey the evaluation order above and happen once. Effects
that complete before a value is discarded, a callee falls through, or a return
is taken remain visible (for example item assignments, persistence operations,
and libcalls). Only an explicit return with an expression exposes a value from
a code item.

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

## Lists and item references

This section defines observable value semantics. List libcall signatures and
failure details remain authoritative in [`libcalls.md`](libcalls.md), while
persistence encoding remains authoritative in
[`itemstore-format.md`](itemstore-format.md).

### Lists

A list literal evaluates each element exactly once, from left to right, and
stores the resulting value. A bare item expression therefore fetches its
value (and executes it when it is a code item); prefixing the expression with
`&` stores an item reference instead and does not fetch or execute the target.
Empty, nested, and expression-position lists are valid.

Lists are immutable values. Assignment, argument passing, explicit returns,
item storage, item reads, and cloning preserve the same logical list value;
whether an implementation shares internal storage is unobservable. Functional
updates such as append, set, concat, and slice return a new list and never
mutate any existing list or alias. Lists may contain every value type,
including other lists and item references. Immutable construction cannot form
a cycle, and list equality is recursive, element-by-element structural
equality. There is no list identity operation; relational comparisons on lists
are unsupported and produce the normal invalid-comparison result. An empty
list is false and every non-empty list is true.

### Item references

`&item` evaluates the item path and creates a reference containing its
canonical root-relative path. A relative path is resolved against the current
item once, at reference creation; the reference does not retain a pointer and
does not later rebase if execution moves elsewhere. Dynamic dereference layers
accept string and integer values. A single leading `nil` or empty string may
stand for an omitted leading layer when a later layer supplies a name; `nil` or
empty alone, and either form in a non-leading position, is invalid. Floats,
booleans, lists, and item references are invalid layer values; an
item-reference value is not itself a path layer. If path assembly or
canonicalization fails, `&item` evaluates to `nil`.

References are weak values. Each item-targeting fetch or call, and each
name-based operation that targets an item, resolves the stored path afresh, so
a dangling reference remains a truthy value and observes an item later
recreated at that same path. Path-inspection operations such as `sys.itemname`
return the stored canonical path without resolving it. Deleting the item does
not rewrite or invalidate the reference. Reference equality compares canonical
paths only, without resolving either path; two references to the same path are
equal even when that path is currently absent.

Item references can be assigned, passed, returned, stored in lists or item
values, and read back without changing these rules. V2 itemstore persistence
stores logical list contents and canonical reference paths, never pointers or
internal sharing; loading restores equivalent values without executing a
referenced target.
