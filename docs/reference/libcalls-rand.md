# Random values (`rand`)

[Libraries and Libcalls](libcalls.md) · [Reference Manual](README.md)

| Call | Result | Arguments |
| --- | --- | --- |
| `rand.int{min, max}` | Uniform integer in the inclusive range `[min, max]`. | Two integers with `min <= max`; the entire signed 64-bit range is supported. |
| `rand.float` | Float in `[0.0, 1.0)`, with 53 random bits. | None. |
| `rand.chance{p}` | Boolean; true when a random float is less than `p`. | Integer or float in `[0, 1]`; zero always returns false and one always returns true. |
| `rand.choice{list}` | Uniformly selected element, or `nil` for an empty list. | A list. |

`rand` is recognized as a library prefix in source, like `list` and `sys`.
All arguments are consumed. `rand.choice` returns an owned value: strings,
item references, and nested lists remain valid after the input is released.
Successful calls and empty-list results preserve an existing `error`.

Wrong types, reversed integer bounds, and probabilities outside `[0, 1]`
(including infinities) return `nil` and set `ERR_RUNTIME_INVALIDARGS` with a
call-specific message. A NaN probability returns `nil` with
`ERR_RUNTIME_UNDEFINED`. A choice cloning or allocation failure returns `nil`
without changing `error`, matching `list.get`.

The generator is xoshiro256**, seeded once per process from strong operating
system entropy before scripts run. Startup fails if entropy is unavailable or
the seed is all zero. Tasks and runtime contexts share a stream; destroying a
context does not reseed it. State is not saved in itemstores, and there is no
script-visible seed control. These calls are suitable for game randomness,
not cryptographic secrets.

```sin
@damage = rand.int{3, 8};
@offset = rand.float;
@critical = rand.chance{0.15};
@direction = rand.choice{#["north", "south", "east", "west"]};
```
