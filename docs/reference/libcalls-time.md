# UTC calendar values (`time`)

[Libraries and Libcalls](libcalls.md) · [Reference Manual](README.md)

| Call | Result | Arguments |
| --- | --- | --- |
| `time.year{milliseconds}` | The UTC Gregorian calendar year containing the Unix epoch timestamp. | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. |
| `time.month{milliseconds}` | The UTC calendar month (1–12). | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. |
| `time.day{milliseconds}` | The UTC day of month (1–31). | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. |
| `time.hour{milliseconds}` | The UTC hour (0–23). | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. |
| `time.minute{milliseconds}` | The UTC minute (0–59). | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. |
| `time.second{milliseconds}` | The UTC second (0–60), after flooring milliseconds. | One integer count of milliseconds since 1970-01-01 00:00:00 UTC. |

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
