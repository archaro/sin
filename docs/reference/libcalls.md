# Libraries and Libcalls

[Reference Manual](README.md) · [Language Reference](language.md) ·
[Error Reference](errors.md) · [Tool Reference](tools.md)

## Invalid argument policy

Libcalls that validate an argument's type, range, or value consume the argument
values, set `error` to `ERR_RUNTIME_INVALIDARGS`, set `error.msg` to a
libcall-specific diagnostic, and return that libcall's documented
invalid-argument value. Current invalid-argument return shapes are `false` for
`sys.compile{source}`, `sys.exists{name}`, `str.contains`, `str.startswith`,
`str.endswith`, and `str.eqcasei`; `nil` for other sys, task, network, string,
and list libcalls that have a typed or range-checked argument.
Any-value calls such as `sys.log`, `str.valtostr`, and `list.islist` accept every
defined value and never take the `ERR_RUNTIME_INVALIDARGS` path for value type.
Failures that are not invalid arguments keep their own contract; for example a
missing task item sets the no-such-item error, an unknown task id returns
`false` without changing `error`, `net.write` to an inactive line returns `nil`
without changing `error`, `net.flush` or `net.ditch` on an inactive line returns
`false` with `ERR_NETWORK_ERROR`, and `str.substr{text, start, len}` with
`len < 1` returns `nil` without changing `error`.

The `math` library accepts numeric values only. A nonnumeric value is consumed,
returns `nil`, and publishes `ERR_RUNTIME_INVALIDARGS` with a math-specific
detail. A numeric value whose mathematical result is not representable returns
`nil` and publishes `ERR_RUNTIME_UNDEFINED`; successful math calls preserve an
existing diagnostic.

Examples:

* `task.newgametask{"heartbeat", -1, 10};` returns `nil`, sets `error` to
  `ERR_RUNTIME_INVALIDARGS`, and writes an interval-range diagnostic to
  `error.msg`.
* `str.upper{42};` returns `nil`, sets `ERR_RUNTIME_INVALIDARGS`, and reports
  that the string argument is invalid.
* `task.killtask{999999};` returns `false` without changing `error` because an
  unknown task id is a domain miss, not an invalid argument.
* `task.thisid` returns the id of the currently executing timer task, or `nil`
  outside a timer-task callback.

## Registered libraries

- [sys](libcalls-sys.md) - essential system calls (compile, backup, etc)
- [str](libcalls-str.md) - string manipulation
- [net](libcalls-net.md) - network operations
- [list](libcalls-list.md) - list operations
- [math](libcalls-math.md) - mathematical operations
- [task](libcalls-task.md) - task handling
