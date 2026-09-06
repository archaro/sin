# UTC calendar values (`time`)

[Libraries and Libcalls](libcalls.md) · [Reference Manual](README.md)

| Libcall | Arguments | Returns | Side effects | Failure behaviour | Example |
| --- | --- | --- | --- | --- | --- |
| `time.year{milliseconds}` | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. | The UTC Gregorian calendar year containing the Unix epoch timestamp. | Consumes the timestamp; performs no persistent mutation. | A non-integer returns `nil` with `ERR_RUNTIME_INVALIDARGS`; an unrepresentable timestamp returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@year = time.year{sys.now};` |
| `time.month{milliseconds}` | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. | The UTC calendar month (1–12). | Consumes the timestamp; performs no persistent mutation. | A non-integer returns `nil` with `ERR_RUNTIME_INVALIDARGS`; an unrepresentable timestamp returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@month = time.month{sys.now};` |
| `time.day{milliseconds}` | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. | The UTC day of month (1–31). | Consumes the timestamp; performs no persistent mutation. | A non-integer returns `nil` with `ERR_RUNTIME_INVALIDARGS`; an unrepresentable timestamp returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@day = time.day{sys.now};` |
| `time.hour{milliseconds}` | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. | The UTC hour (0–23). | Consumes the timestamp; performs no persistent mutation. | A non-integer returns `nil` with `ERR_RUNTIME_INVALIDARGS`; an unrepresentable timestamp returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@hour = time.hour{sys.now};` |
| `time.minute{milliseconds}` | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. | The UTC minute (0–59). | Consumes the timestamp; performs no persistent mutation. | A non-integer returns `nil` with `ERR_RUNTIME_INVALIDARGS`; an unrepresentable timestamp returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@minute = time.minute{sys.now};` |
| `time.second{milliseconds}` | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. | The UTC second (0–60), after flooring milliseconds. | Consumes the timestamp; performs no persistent mutation. | A non-integer returns `nil` with `ERR_RUNTIME_INVALIDARGS`; an unrepresentable timestamp returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@second = time.second{sys.now};` |

`time` is recognized as a library prefix in source. The timestamp is floored
to the containing Unix second, so negative sub-second values are in the
preceding second (`time.year{-1}` returns `1969`). The conversion is explicitly
UTC and does not use the host local timezone.

A non-integer argument is consumed and returns `nil` with
`ERR_RUNTIME_INVALIDARGS` and a call-specific `time.*` diagnostic. If the host cannot
represent the timestamp as a UTC calendar value, the call returns `nil` with
`ERR_RUNTIME_UNDEFINED`.

```sin
@year = time.year{sys.now};
@minute = time.minute{sys.now};
```
