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
