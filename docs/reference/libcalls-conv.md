# Value conversions (`conv`)

[Libraries and Libcalls](libcalls.md) · [Reference Manual](README.md)

`conv` provides explicit conversions from runtime values to canonical boolean,
integer, floating-point, and string values.

| Libcall | Arguments | Returns | Side effects | Failure behaviour | Example |
| --- | --- | --- | --- | --- | --- |
| `conv.bool{value}` | Any runtime value. | `false` for `nil`, zero numbers, empty strings, and empty lists; `true` for non-zero numbers, non-empty strings/lists, and item references. | Consumes the argument and does not mutate the itemstore. | All defined value types, including malformed/null list payloads, are accepted; no diagnostic is published. | `@enabled = conv.bool{@setting};` |
| `conv.int{value}` | An integer, finite float in the signed integer range, boolean, valid integer string, or `nil`. | The integer unchanged; a float truncated toward zero; `false`/`true` as `0`/`1`; a parsed string; or `0` for `nil`. | Consumes the argument and does not mutate the itemstore. | Lists, item references, malformed values, invalid or out-of-range strings, and non-finite or out-of-range floats return `nil` with `ERR_RUNTIME_INVALIDARGS`. Integer strings must contain only an optional sign and decimal digits; surrounding or trailing whitespace is invalid. | `@count = conv.int{"42"};` |
| `conv.float{value}` | An integer, float, boolean, valid decimal float or integer string, or `nil`. | A float unchanged; an integer converted to binary64; `false`/`true` as `0.0`/`1.0`; a parsed string; or `0.0` for `nil`. | Consumes the argument and does not mutate the itemstore. | Lists, item references, malformed values, unparseable strings, and strings whose parsed result is non-finite return `nil` with `ERR_RUNTIME_INVALIDARGS`. A string must be either an optional sign followed by decimal digits, or an optional sign followed by decimal digits, a dot, decimal digits, and an optional signed decimal exponent. Thus `"1e3"`, `".5"`, and `"1."` are rejected, while `"1.0e3"` succeeds. Parsing consumes the entire input; surrounding or trailing whitespace is invalid. | `@ratio = conv.float{"123.45"};` |
| `conv.str{value}` | Any runtime value. | The value rendered as canonical text: strings pass through unchanged; scalars, item references, and lists use the same rendering as `str.valtostr`. | Consumes non-string values and does not mutate the itemstore; strings retain their existing stack ownership. | Malformed values or rendering/allocation failure return `nil`; no invalid-argument diagnostic is published. | `@label = conv.str{42};` |

The conversion uses Sinistra's ordinary truthiness rules, including treating
both signed zero and NaN according to the runtime value helper.
