# The list library

[Reference Manual](README.md) · [Libcall index](libcalls.md) ·
[Error Reference](errors.md)

List operations for constructing and transforming immutable list values.

Note that lists are immutable.  Combine list libcalls with `sys.itemref`,
`sys.itemname`, `sys.fetch`, and `sys.call` when a list carries references
that should be inspected or invoked explicitly.

| Libcall | Arguments | Returns | Side effects | Failure behaviour | Example |
| --- | --- | --- | --- | --- | --- |
| `list.length{list}` | `list` must evaluate to a list. | The element count as an integer. | Consumes the argument without changing the list. | A non-list argument sets `ERR_RUNTIME_INVALIDARGS` and returns `nil`. | `@count = list.length{@values};` |
| `list.get{list, index}` | `list` must be a list and `index` an integer zero-based index. | An owned clone of the selected value. | Consumes the arguments without changing the list. | Wrong types set `ERR_RUNTIME_INVALIDARGS`. A negative or out-of-range index, ownership failure, or allocation failure returns `nil` without changing `error`. | `@first = list.get{@values, 0};` |
| `list.append{list, value}` | `list` must be a list; `value` may have any value type. | A new list with `value` appended. | Preserves the input list and provides immutable value semantics. | A non-list first argument sets `ERR_RUNTIME_INVALIDARGS`. A size, depth, ownership, or allocation failure returns `nil` without changing `error`. | `@more = list.append{@values, 4};` |
| `list.set{list, index, value}` | `list` must be a list, `index` an integer zero-based index, and `value` may have any value type. | A new list with the selected element replaced. | Preserves the input list and provides immutable value semantics. | Wrong list/index types set `ERR_RUNTIME_INVALIDARGS`. A negative or out-of-range index, depth failure, ownership failure, or allocation failure returns `nil` without changing `error`. | `@changed = list.set{@values, 1, 9};` |
| `list.concat{left, right}` | Both arguments must be lists. | A new list containing `left` followed by `right`. | Preserves both inputs and provides immutable value semantics. | Wrong types set `ERR_RUNTIME_INVALIDARGS`. A combined-size, depth, ownership, or allocation failure returns `nil` without changing `error`. | `@all = list.concat{@first, @second};` |
| `list.slice{list, start, length}` | `list` must be a list; `start` and `length` must be non-negative integers whose half-open range fits wholly in the list. `start` may equal the list length only when `length` is zero. | A new list containing the requested range; a zero length returns an empty list. | Preserves the input list and provides immutable value semantics. | Wrong types set `ERR_RUNTIME_INVALIDARGS`. Negative or out-of-range values, ownership failure, or allocation failure return `nil` without changing `error`. | `@middle = list.slice{@values, 1, 2};` |
| `list.islist{value}` | `value` may have any value type. | `true` when `value` is a list; `false` otherwise. | Consumes the argument. | Cannot fail; never changes `error`. | `@ok = list.islist{@maybe};` |
