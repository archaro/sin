# text library

The `text` library provides byte-oriented operations on owned strings and lists.

`text.split` divides text into an owned list of strings:

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

`text.join` combines a homogeneous list of non-null strings with a separator:

```sinistra
line = text.join{#["one", "two", "three"], "::"};
```

The list and separator are required and both are consumed. The output contains
each field exactly as stored, with the separator only between fields; empty
fields and empty separators are preserved. An empty list returns an owned empty
string. The returned string always owns independent storage.

Invalid list elements, invalid arguments, and malformed null string payloads
return `nil` and set `ERR_RUNTIME_INVALIDARGS` with a `text.join` detail.
Output at or below `SIN_MAX_STRING_BYTES` succeeds. Larger output, overflow,
and allocation failure return `nil`, clean up both arguments, and preserve any
unrelated existing diagnostic.

`text.words` splits a string into words using C byte whitespace:

```sinistra
words = text.words{"one\ttwo\nthree"};
```

The call requires exactly one non-null string argument. Spaces, tabs, newlines,
vertical tabs, form feeds, and carriage returns delimit fields; empty fields
are omitted. Quotes, punctuation, backslashes, and UTF-8 bytes are copied
literally. Empty and all-whitespace input returns an owned empty list.

The input is consumed, and the returned list and every string element own
independent storage. Invalid values and malformed null string payloads return
`nil` with `ERR_RUNTIME_INVALIDARGS` and a `text.words` detail. String/list
limits, overflow, allocation, and list-construction failures return `nil` after
cleanup while preserving unrelated diagnostics.

`text.lines` splits a string into non-empty lines:

```sinistra
lines = text.lines{"first\nsecond\r\nthird"};
```

The call requires exactly one non-null string argument. LF bytes terminate
lines; an immediately preceding CR is consumed as part of a CRLF terminator.
Bare CR bytes and every other byte remain in the returned field. Leading,
trailing, and repeated LF/CRLF terminators omit empty fields. Empty and
all-terminator input returns an owned empty list.

The input is consumed, and the returned list and every string element own
independent storage. Invalid values and malformed null string payloads return
`nil` with `ERR_RUNTIME_INVALIDARGS` and a `text.lines` detail. String/list
limits, overflow, allocation, and list-construction failures return `nil` after
releasing staged ownership while preserving unrelated diagnostics.

`text.condense` normalizes C byte whitespace in a string:

```sinistra
summary = text.condense{"  one\ttwo\nthree  "};
```

The call requires exactly one non-null string argument. Leading and trailing
whitespace is removed, and every interior run of spaces, tabs, newlines,
vertical tabs, form feeds, or carriage returns becomes one ASCII space.
Non-whitespace bytes, including punctuation and UTF-8 bytes, are copied
literally. Empty and all-whitespace input return separately owned empty
strings.

The input is consumed and the returned string has independent ownership.
Invalid values and malformed null string payloads return `nil` with
`ERR_RUNTIME_INVALIDARGS` and a `text.condense` detail. Too-large input,
overflow, and allocation failure return `nil` after cleanup while preserving
unrelated diagnostics.
