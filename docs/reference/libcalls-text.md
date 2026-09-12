# text library

`text.split` divides byte-oriented text into an owned list of strings:

```sinistra
parts = text.split{"one::two::three", "::"};
```

The call requires exactly two string arguments: the text followed by a
separator. The separator is interpreted literally as a whole, case-sensitive
substring and matches are selected non-overlapping from left to right. Leading,
trailing, and repeated separators omit empty fields. Non-separator whitespace
and UTF-8 bytes are preserved unchanged; this operation does not perform
Unicode normalization or grapheme processing.

An empty separator returns a singleton containing a copy of the original text,
including when the original text is empty. A non-empty separator with no match
also returns a singleton for non-empty input; empty input with a non-empty
separator returns a real empty list. An input consisting only of separators
also returns a real empty list.

Both arguments are consumed. The returned list and every returned string own
their storage, so results remain valid after the input values are released.
Invalid types, `nil`, and malformed null string payloads return `nil` and set
`ERR_RUNTIME_INVALIDARGS` with a `text.split` detail. Allocation, list-limit,
and list-construction failures return `nil`, clean up staged values, and retain
any unrelated existing diagnostic.

Unlike `str` calls, which provide byte-string manipulation and in-place text
operations, `text.split` is a block-producing operation whose result is an
immutable runtime list.
