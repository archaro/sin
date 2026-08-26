# The Failure Model - When Things Go Wrong

If you can keep your head when all around are losing theirs, then you have
clearly misunderstood the situation.

The rest of the guide talks about what happens when things go *right*: code
compiles, tools run to completion, every item holds precisely the value it is
meant to hold.  But, alas, we live in an imperfect world.  Sometimes things go
*wrong*, even in Sinistra.

## Values and Errors
Sinistra does not have exceptions. Expressions and calls produce ordinary
values, and `nil` is itself an ordinary value. Some operations also record
diagnostic state in the `error` item and its children.

A failure value and an error diagnostic are not the same thing. An operation
may return `nil` or `false` without setting `error`, and an operation may set
`error` while retaining its normal result. The exact contract belongs to the
operation concerned.

## The error Item
The runtime uses the `error` item and its children to record diagnostic state.
Not every unsuccessful or exceptional-looking result sets `error`; whether an
operation records a diagnostic is part of that operation's contract.

Important note: Success does not universally clear an earlier error.  Some
calls deliberately preserve unrelated existing diagnostics. `sys.compile` is a
notable exception: successful compilation/execution normalises `error` and all
its structured children to `nil`.

## Errors and Control Flow

Setting `error` does not throw an exception or automatically unwind the current
code item. The diagnostic is state in the item tree. Unless the operation
causes an interpreter-level abort, execution continues according to normal
control flow.

Because code-item calls are synchronous, diagnostic state recorded by a callee
remains part of the shared item tree and is visible to its caller unless it has
subsequently been cleared or replaced.

## Compile-time Failures
The compiler is reasonably intelligent and tries to be helpful about errors when
it encounters them.  Compilation can fail in two contexts. When using `scomp`,
the compiler reports a structured diagnostic to standard error and exits
non-zero. When compilation is requested from a running world through
`sys.compile`, the same kind of diagnostic is made available through the
`error` subtree.

Within a running world, a compiler failure sets the numeric `error` value and
may populate the following structured fields:
- `error.code`: The stable compiler diagnostic code
- `error.stage`: The stage of compilation where the error was encountered
- `error.file`: The filename where the error was encountered (`<memory>` when
compiling a string submitted through `sys.compile`)
- `error.line`, `error.column`: The location of the error
- `error.excerpt`: An excerpt of code near to the error
- `error.msg`: Human-readable diagnostic text

Compiler diagnostics describe the submitted source rather than a running code
item, so `error.item` is not used as the compiler source location.

On successful compilation and execution, `sys.compile` normalises the complete
structured error state to `nil`; on failure, the relevant compiler or runtime
diagnostic remains observable.

## Runtime Failures
Ordinary runtime errors, by their nature, have less diagnostic information
available. When a runtime operation reports a diagnostic through `error`, the
basic fields are:

- `error`: The error number
- `error.msg`: An explanatory message
- `error.item`: The code item in which the error was encountered, when available

Recording an ordinary runtime error clears compiler-specific diagnostic fields, so stale compiler location information is not mistaken for part of the new runtime error.

## Failure and Side Effects
Synchronous code-item calls do not provide transactional rollback. Side effects
which occurred before failure remain visible. Returning `nil`, falling through,
setting `error`, and aborting execution are distinct things and must not be
conflated.

## Tolerant Contracts and Strict Diagnostics
Sinistra deliberately tolerates some item-call mismatches to support live
updating. Missing parameters receive `nil`; excess arguments are discarded.
Arguments supplied to value, missing, or invalid targets may likewise be
consumed while the call retains its normal result.

`--strict-runtime-contracts` does not change these call results. Where
arguments have to be discarded, it additionally records and logs the contract
violation.

## Practical Rules
When code fails, either at the compilation stage or at runtime, ask three
questions:
- What value is returned?
- What happens to `error`?
- What side effects may already have occurred?

Consult [Troubleshooting](troubleshooting.md) for diagnosis, and the [canonical
reference manual](../reference/) for exact per-call contracts.
