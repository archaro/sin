# Error Reference

This page is the reference for the runtime-managed `error` namespace and its
numeric error codes. For the conceptual distinction between failures, return
values, and diagnostics, see the [Failure Model](../guide/failure-model.md).
The exact return values, side effects, and error-setting rules for each
operation are in the [libcall reference](libcalls.md) and the [sys](libcalls-sys.md),
[str](libcalls-str.md), [net](libcalls-net.md), [list](libcalls-list.md),
[task](libcalls-task.md), and [math](libcalls-math.md) library pages.

[Reference Manual](README.md) · [Language Reference](language.md) ·
[Libcalls](libcalls.md) · [Tool Reference](tools.md)

## Error codes

When a diagnostic is published, `error` is its integer error code. The
defined `ERR_NOERROR` value is integer `0`; it is distinct from the runtime's
normalized no-diagnostic state, in which all nine managed nodes are `nil`.
The following names and default messages are defined by the engine. Numeric
values not assigned a name remain unused; they are shown so that the numeric
gaps are explicit.

| Number | Symbol | Default message |
| ---: | --- | --- |
| 0 | `ERR_NOERROR` | No error. |
| 1 | `ERR_COMP_SYNTAX` | Syntax error. |
| 2 | *(unused)* | — |
| 3 | `ERR_COMP_TOOMANYLOCALS` | Too many local variables. |
| 4 | `ERR_COMP_LOCALBEFOREDEF` | Local used before definition. |
| 5 | `ERR_COMP_UNKNOWNCHAR` | Unknown character in input. |
| 6 | *(unused)* | — |
| 7 | *(unused)* | — |
| 8 | `ERR_COMP_INUSE` | Item in use; cannot replace it. |
| 9 | `ERR_COMP_TOOMANYPARAMS` | Too many parameters in item definition. |
| 10 | `ERR_COMP_TOOMANYARGS` | Too many arguments passed to item. |
| 11 | `ERR_COMP_UNKNOWN` | Unknown compiler error. |
| 12 | *(unused)* | — |
| 13 | *(unused)* | — |
| 14 | *(unused)* | — |
| 15 | *(unused)* | — |
| 16 | *(unused)* | — |
| 17 | *(unused)* | — |
| 18 | *(unused)* | — |
| 19 | *(unused)* | — |
| 20 | `ERR_RUNTIME_SIGUSR1` | Restarting due to SIGUSR1. |
| 21 | `ERR_RUNTIME_INVALIDARGS` | Invalid arguments to library call. |
| 22 | `ERR_RUNTIME_NOSUCHITEM` | Item does not exist. |
| 23 | `ERR_RUNTIME_TRUNCATED` | Truncated bytecode. |
| 24 | `ERR_RUNTIME_INVLIB` | Invalid libcall. |
| 25 | `ERR_RUNTIME_BYTECODE` | Invalid bytecode. |
| 26 | `ERR_RUNTIME_INVALIDITEM` | Invalid item name. |
| 27 | `ERR_RUNTIME_INTERNAL` | Internal runtime error. |
| 28 | `ERR_RUNTIME_PERSISTENCE` | Itemstore persistence failed. |
| 29 | `ERR_RUNTIME_SOURCE` | Item source is unavailable. |
| 30 | `ERR_NETWORK_ERROR` | Network error. |
| 31 | `ERR_RUNTIME_INUSE` | Item or descendant is execution-pinned; cannot delete. |
| 32 | `ERR_RUNTIME_UNDEFINED` | Undefined mathematical result. |

An operation may add detail to the default message. A return value of `nil` or
`false` does not by itself imply that an error was published; consult the
operation-specific contract.

## Runtime-managed error namespace

The runtime owns the root `error` item and every descendant. The nine managed
nodes are:

| Node | Meaning |
| --- | --- |
| `error` | Numeric error code. |
| `error.msg` | Human-readable diagnostic string. |
| `error.item` | String name of the current executing item when provenance is available; otherwise `nil`. |
| `error.code` | Compiler-only stable diagnostic code; `nil` for runtime errors. |
| `error.stage` | Compiler-only stage name; `nil` for runtime errors. |
| `error.file` | Compiler-only source filename; `nil` for runtime errors. |
| `error.line` | Compiler-only source line; `nil` for runtime errors. |
| `error.column` | Compiler-only source column; `nil` for runtime errors. |
| `error.excerpt` | Compiler-only source excerpt; `nil` for runtime errors. |

When the runtime initializes or normalizes this namespace, all nine paths are
present as value items. Sinistra code may read them like other item values, but
assignments, code replacement, and deletion are rejected.

Assignments, code replacement, and deletion of `error` or any `error.*`
descendant are rejected. This includes names reached through relative paths,
mixed case, and descendants not currently present. The attempted operation
does not replace or remove the protected node and publishes the invalid-item
diagnostic.

### Runtime and compiler shapes

An ordinary runtime publication sets `error` to its numeric code, `error.msg`
to a string (the default message, optionally with operation detail), and
`error.item` to the current executing item name when one is available. If no
current item exists, `error.item` is `nil`. It clears the compiler-only
`error.code`, `error.stage`, `error.file`, `error.line`, `error.column`, and
`error.excerpt` nodes to `nil`.

A compiler diagnostic instead sets `error` to the numeric compiler diagnostic
code and `error.msg` to a formatted diagnostic. `error.item` is always `nil`.
The compiler-only `error.code`, `error.stage`, `error.file`, and
`error.excerpt` fields are strings and may be empty; `error.line` and
`error.column` are integers, with `0` meaning that the diagnostic has no
location. A compiler failure without a diagnostic record falls back to the
unknown-compiler error (`ERR_COMP_UNKNOWN`) with the ordinary runtime shape.

`error.item` records provenance, not the source location of a compiler
diagnostic. A runtime error from `sys.compile` can retain the generated
temporary item's name in this field even after that temporary item has been
deleted. Compiler diagnostics produced by `sys.compile` keep `error.item` as
`nil`.

### Clearing and success

Clearing the error state (implemented internally by `clear_error_item`)
normalizes all nine managed nodes to `nil`. Successful `sys.compile` performs
the same normalization after the generated code has finished. Success does not
universally clear an existing diagnostic: error preservation is
operation-specific. See the [libcall tables](libcalls.md) for each operation's
exact return, error, and side-effect contract.

## Strict runtime contracts

With `--strict-runtime-contracts`, tolerant item-call result and stack behavior
does not change. Missing parameters are still filled with `nil`, and extra
arguments are still discarded. The option additionally publishes and logs
`ERR_RUNTIME_INVALIDARGS` when an argument is discarded because of an
over-arity call, or because the target is a value item, missing, or invalid.
It does not turn these tolerated calls into a different return or stack
contract. See the [tool options](tools.md) and [language item-call rules](language.md).
