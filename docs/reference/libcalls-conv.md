# Value conversions (`conv`)

[Libraries and Libcalls](libcalls.md) · [Reference Manual](README.md)

`conv` provides explicit conversions from runtime values to canonical boolean
values.

| Libcall | Arguments | Returns | Side effects | Failure behaviour | Example |
| --- | --- | --- | --- | --- | --- |
| `conv.bool{value}` | Any runtime value. | `false` for `nil`, zero numbers, empty strings, and empty lists; `true` for non-zero numbers, non-empty strings/lists, and item references. | Consumes the argument and does not mutate the itemstore. | All defined value types, including malformed/null list payloads, are accepted; no diagnostic is published. | `@enabled = conv.bool{@setting};` |

The conversion uses Sinistra's ordinary truthiness rules, including treating
both signed zero and NaN according to the runtime value helper.
