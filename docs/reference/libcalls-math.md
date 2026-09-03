# The math library

[Reference Manual](README.md) · [Libcall index](libcalls.md) ·
[Error Reference](errors.md)

Mathematical operations accept numeric VM values and preserve an existing
runtime diagnostic on success. Nonnumeric values are consumed and rejected with
`ERR_RUNTIME_INVALIDARGS`; numeric inputs whose result is not representable
are consumed and return `nil` with `ERR_RUNTIME_UNDEFINED`. `math.sqrt` rejects
negative values (including negative infinity) with `ERR_RUNTIME_INVALIDARGS`,
while NaN and positive infinity are undefined. `math.pow` treats NaN and either
infinity in either argument, domain errors, and non-finite results as undefined.
`math.log`, `math.log2`, and `math.log10` require a strictly positive numeric
input; zero, negative values (including negative zero and negative infinity),
and nonnumeric values are invalid, while NaN, positive infinity, and
non-finite results are undefined.
The unary rounding operations always return integers. `math.round` rounds
exact halfway cases away from zero. Float results are accepted only in the
binary64-safe interval `[-0x1p63, 0x1p63)` after rounding.

| Libcall | Library | Call | Arity | Argument expectations | Return value | Side effects | Failure behaviour | Example |
| --- | --- | --- | ---: | --- | --- | --- | --- |
| `math.abs{value}` | `math` | `abs` | 1 | `value` must be an integer or float. | The same numeric type with its non-negative magnitude; `+0.0` is returned for either signed float zero. | Consumes the input value. | A nonnumeric input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math-specific detail. `INT64_MIN`, NaN, and either infinity are valid numeric inputs whose undefined result returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@magnitude = math.abs{@value};` |
| `math.min{left, right}` | `math` | `min` | 2 | Both values must be integers or floats. | The lesser input as an integer when both inputs are integers, otherwise as a float; float comparisons use `fmin` signed-zero semantics. | Consumes both input values. | A nonnumeric input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.min-specific detail. NaN or either infinity returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@smaller = math.min{@left, @right};` |
| `math.max{left, right}` | `math` | `max` | 2 | Both values must be integers or floats. | The greater input as an integer when both inputs are integers, otherwise as a float; float comparisons use `fmax` signed-zero semantics. | Consumes both input values. | A nonnumeric input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.max-specific detail. NaN or either infinity returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@larger = math.max{@left, @right};` |
| `math.floor{value}` | `math` | `floor` | 1 | `value` must be an integer or float. | The greatest integral value no greater than `value`, returned as an integer. | Consumes the input value. | A nonnumeric input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.floor-specific detail. NaN, either infinity, or an out-of-range rounded result returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@lower = math.floor{@value};` |
| `math.ceil{value}` | `math` | `ceil` | 1 | `value` must be an integer or float. | The least integral value no less than `value`, returned as an integer. | Consumes the input value. | A nonnumeric input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.ceil-specific detail. NaN, either infinity, or an out-of-range rounded result returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@upper = math.ceil{@value};` |
| `math.round{value}` | `math` | `round` | 1 | `value` must be an integer or float. | The nearest integral value, with exact halfway cases rounded away from zero, returned as an integer. | Consumes the input value. | A nonnumeric input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.round-specific detail. NaN, either infinity, or an out-of-range rounded result returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@nearest = math.round{@value};` |
| `math.sqrt{value}` | `math` | `sqrt` | 1 | `value` must be an integer or float and non-negative. | The square root, always returned as a float; negative zero is accepted. | Consumes the input value. | A nonnumeric or negative input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.sqrt-specific detail. NaN, positive infinity, or a non-finite result returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@root = math.sqrt{@value};` |
| `math.pow{base, exponent}` | `math` | `pow` | 2 | Both values must be integers or floats. | `base` raised to `exponent`, always returned as a float. | Consumes both input values. | A nonnumeric input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.pow-specific detail. NaN, either infinity, a domain error, or a non-finite result returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@power = math.pow{@base, @exponent};` |
| `math.log{value}` | `math` | `log` | 1 | `value` must be an integer or float greater than zero. | The natural logarithm, always returned as a float. | Consumes the input value. | A nonnumeric or non-positive input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.log-specific detail. NaN, positive infinity, or a non-finite result returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@natural = math.log{@value};` |
| `math.log2{value}` | `math` | `log2` | 1 | `value` must be an integer or float greater than zero. | The base-2 logarithm, always returned as a float. | Consumes the input value. | A nonnumeric or non-positive input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.log2-specific detail. NaN, positive infinity, or a non-finite result returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@bits = math.log2{@value};` |
| `math.log10{value}` | `math` | `log10` | 1 | `value` must be an integer or float greater than zero. | The base-10 logarithm, always returned as a float. | Consumes the input value. | A nonnumeric or non-positive input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.log10-specific detail. NaN, positive infinity, or a non-finite result returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@digits = math.log10{@value};` |
