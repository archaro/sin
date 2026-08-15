# Troubleshooting

This guide covers the diagnostics and generated local state used by the command
line tools. See [`tools.md`](../reference/tools.md) for the complete option reference.

## Compiler diagnostics

When `scomp` cannot compile an input, it exits nonzero and writes a structured
diagnostic to standard error. It includes a stable code, stage, file, line,
column, message, `ERR` number, and source excerpt with a caret marker when a
location is available. Stable codes have the form `SIN-<PHASE>-<4-digit>`.
Current phases are `PARSE`, `SEMANT`, `LOWER`, `IR_VALIDATE`, `EMITBC`,
`COMPILE`, and `IO`.

Locations from semantic analysis, lowering, IR validation, and bytecode
emission are preserved when the failing construct came from source. The
excerpt therefore shows the responsible source line; genuinely global setup
or allocation failures remain locationless (and use the standard fallback
only when no source provenance exists).

Use the stable code and source location when reporting or matching a compiler
failure; the prose message can become more specific over time. `-q` suppresses
progress and status messages, while `-v` adds verbose progress and diagnostic
trace messages. Errors still go to standard error.

## Runtime error items

Runtime failures are recorded in the item tree. The basic fields are `error`
(the numeric error), `error.msg` (the description), and `error.item` (the
executing item when available). Structured compiler failures also populate
`error.code`, `error.stage`, `error.file`, `error.line`, `error.column`, and
`error.excerpt`; these identify the stable code, compiler phase, source
location, and source text. Ordinary runtime errors mainly use `error`,
`error.msg`, and `error.item`, and clear the compiler-only fields.

Do not assume that success clears an earlier error: that behavior is specific to
each call's contract. Check the relevant entry in [`libcalls.md`](../reference/libcalls.md)
when code needs to clear or preserve a diagnostic deliberately.

## Logging

`scomp` and `sin` support normal, quiet (`-q`), and verbose (`-v`) logging.
`sdiss` accepts `-q` to suppress progress messages and `-v` for parity, but
`-v` currently produces no additional trace output. Normal output is written
to standard output and errors to standard error; quiet mode suppresses
progress/status output and verbose mode adds diagnostic trace output for the
tools that emit it.

Only `sin` supports `-l`/`--log`, with an optional base name (default: `sin`).
It appends normal standard-output messages to `<base>.log`, and error and
verbose output to `<base>.err`.

## Unreadable itemstores

An existing itemstore that cannot be read is refused and is not replaced. Keep
that file intact: copy it before investigating or recovering it, and do not
delete it in an attempt to make `sin` start. A new store is created only when
the selected itemstore path does not already exist.

## Resetting disposable local state

For a disposable local experiment using the defaults, first stop `sin`, then
move the exact generated paths aside using unused backup names:

```sh
mv items.dat items.dat.before-reset
mv srcroot srcroot.before-reset
```

The next default run can create fresh `items.dat` and `srcroot` state. These
commands deliberately preserve the previous state rather than deleting it. If
you used `-i`/`--itemstore` or `-s`/`--srcroot`, those user-selected paths are
not disposable defaults: preserve and handle them explicitly instead of using
this reset procedure.
