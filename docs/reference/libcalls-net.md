# The net library

[Reference Manual](README.md) · [Libcall index](libcalls.md) ·
[Error Reference](errors.md)

This library handles interactions with telnet connections.

## Network line contracts

Network lines are zero-based connection slots. The configured maximum connection
count defines the valid line-number range `0..maxconns-1`. A slot keeps its line
number for its lifetime and can be reused after a disconnect has been reported
and the line resources have been destroyed.

`net.input` processes at most one network event per call. Each call advances a
fair-queue cursor before scanning for the next line with a pending event:

* `1` means a new connection. The line moves from connecting to idle and
  `input.line` is set to the zero-based line number.
* `2` means a disconnection. `input.line` is set only after close completion
  and pending writes settle; the line is then destroyed and the slot becomes
  reusable before `net.input` returns.
* `3` means a complete input line. `input.line` is set and `input.text` receives
  the line text without the terminating newline.
* `0` means no connection, disconnection, or complete input line is pending.

`net.ditch{line}` is a graceful local disconnect request. It accepts only active
lines: connecting, idle, or data. It marks the line for disconnection, flushes
pending output, and closes the handle immediately only when no output remains
queued or in flight. The later `net.input` disconnection event is what destroys
the line and makes the slot reusable; close completion and pending writes are
settled before this event. Repeating `net.ditch` on an already
disconnecting or empty line returns `false` and sets the network error item.

| Libcall | Library | Call | Arity | Argument expectations | Return value | Side effects | Failure behaviour | Example |
| --- | --- | --- | ---: | --- | --- | --- | --- | --- |
| `net.input` | `net` | `input` | 0 | None. | Integer activity code: `0` for no activity, `1` for a new connection, `2` for a disconnection, or `3` for received input data. | Advances the fair-queue connection cursor.  For connection, disconnection, and data events, best-effort publishes the configured input line item, normally `input.line`, with the zero-based line number.  For data events, it also best-effort publishes the complete received line of text, without its terminating newline, to the configured input text item, normally `input.text`.  A disconnection event destroys the line and returns the slot to the reusable state before returning. | Publication failures are not reported to Sinistra code: the network event is still consumed and its activity code is returned. No activity returns `0`. | `@event = net.input;` |
| `net.maxlines` | `net` | `maxlines` | 0 | None. | Configured maximum connection-slot count as an integer. | Bounds enumeration of zero-based connection slots: valid slot indexes are `0..net.maxlines-1`. | Success preserves unrelated prior errors. | `@slots = net.maxlines;` |
| `net.connected{line}` | `net` | `connected` | 1 | `line` must evaluate to an in-range, non-negative integer connection index; floats are invalid for `line`. | `true` only when the line has a Telnet session and is idle or has data; `false` for connecting, disconnecting, empty, missing-Telnet, or otherwise unavailable lines; `nil` for invalid line arguments. | Safe predicate for checking slots while enumerating `0..net.maxlines-1`; has no side effects. | Invalid type, negative, or out-of-range lines set the runtime invalid-arguments error and return `nil`. Valid true/false results preserve any unrelated prior error. | `@online = net.connected{3};` |
| `net.address{line}` | `net` | `address` | 1 | `line` must evaluate to an in-range, non-negative integer connection index; floats are invalid for `line`. | Numeric peer-address string for a writable Telnet line with an address; `nil` for unavailable or empty-address lines, invalid line arguments, or address-string allocation failure. | Reads the peer's numeric IPv4 or IPv6 address. Retain the returned string in game code before `net.input` reports disconnection if it is needed afterward. | Invalid type, negative, or out-of-range lines set the runtime invalid-arguments error and return `nil`. Valid string/nil results, including allocation failure, preserve any unrelated prior error. | `@address = net.address{input.line};` |
| `net.write{line, value}` | `net` | `write` | 2 | Line must be an in-range integer; value may be any renderable value, including lists (`#[...]`) and item references (`&path`), with nested string escaping; nil writes nothing. | `nil` when output is accepted/queued or is a no-op; `false` when rendering or immediate output-limit/backpressure checks reject it. | Queues fully rendered text for asynchronous transport; partial aggregate output is never queued. Acceptance does not guarantee delivery: a later asynchronous transport failure is logged and causes the line to disconnect. | Invalid lines return `nil` with invalid-arguments diagnostics; rendering or immediate output-limit/backpressure rejection returns `false`. A later transport failure is not reported through this return value. | `net.write{input.line, "Hello!\n"};` |
| `net.echo{value}` | `net` | `echo` | 1 | `value` uses normal Sinistra truthiness. | Always `nil`. | Negotiates Telnet ECHO for the writable idle/data line most recently selected by `net.input`. Truthy sends `IAC WONT ECHO`, leaving local/client echo on; falsy sends `IAC WILL ECHO`, so the server claims echo and local/client echo turns off. Sinistra does not echo received input itself. | An out-of-range cursor or a connecting, disconnecting, empty, or missing-Telnet line is a no-op without changing `error`. | `net.echo{false};` |
| `net.flush{line}` | `net` | `flush` | 1 | `line` must evaluate to a non-negative integer connection index; floats are invalid for `line`. | `true` when the selected connecting, idle, or data line is active and a flush was requested; `false` when the line index is out of range or the line is inactive/disconnecting; `nil` for invalid argument types or negative indexes. | Requests an immediate flush of pending output for the selected line. | Invalid argument types or negative indexes set the runtime invalid-arguments error and return `nil`. Out-of-range or inactive/disconnecting lines set the network error item and return `false`. | `net.flush{input.line};` |
| `net.ditch{line}` | `net` | `ditch` | 1 | `line` must evaluate to a non-negative integer connection index; floats are invalid for `line`. | `true` when the selected connecting, idle, or data line is active and is marked for disconnection; `false` when the line index is out of range or the line is already empty/disconnecting; `nil` for invalid argument types or negative indexes. | Marks the selected active line as disconnecting. Pending output is flushed first; the libuv handle is closed immediately only when no output is queued or in flight. The later network input pass reports the disconnection event, destroys the line, and makes the zero-based slot reusable. | Invalid argument types or negative indexes set the runtime invalid-arguments error and return `nil`. Out-of-range or already inactive/disconnecting lines set the network error item and return `false`. | `net.ditch{input.line};` |
