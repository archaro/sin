# The math library

[Reference Manual](README.md) · [Libcall index](libcalls.md) ·
[Error Reference](errors.md)

Mathematical operations accept numeric VM values and preserve an existing
runtime diagnostic on success. Nonnumeric values are consumed and rejected with
`ERR_RUNTIME_INVALIDARGS`; numeric inputs whose result is not representable
are consumed and return `nil` with `ERR_RUNTIME_UNDEFINED`.

| Libcall | Library | Call | Arity | Argument expectations | Return value | Side effects | Failure behaviour | Example |
| --- | --- | --- | ---: | --- | --- | --- | --- |
| `math.abs{value}` | `math` | `abs` | 1 | `value` must be an integer or float. | The same numeric type with its non-negative magnitude; `+0.0` is returned for either signed float zero. | Consumes the input value. | A nonnumeric input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math-specific detail. `INT64_MIN`, NaN, and either infinity are valid numeric inputs whose undefined result returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@magnitude = math.abs{@value};` |
| `math.min{left, right}` | `math` | `min` | 2 | Both values must be integers or floats. | The lesser input as an integer when both inputs are integers, otherwise as a float; float comparisons use `fmin` signed-zero semantics. | Consumes both input values. | A nonnumeric input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.min-specific detail. NaN or either infinity returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@smaller = math.min{@left, @right};` |
| `math.max{left, right}` | `math` | `max` | 2 | Both values must be integers or floats. | The greater input as an integer when both inputs are integers, otherwise as a float; float comparisons use `fmax` signed-zero semantics. | Consumes both input values. | A nonnumeric input returns `nil` and sets `ERR_RUNTIME_INVALIDARGS` with math.max-specific detail. NaN or either infinity returns `nil` with `ERR_RUNTIME_UNDEFINED`. | `@larger = math.max{@left, @right};` |
