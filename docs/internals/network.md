# Network Internals

This page describes Sinistra's network runtime: connection ownership, event
delivery, buffering, Telnet integration, and the boundary between transport
code and the Sinistra-visible `net.*` library.

Network callbacks run on the same single-threaded `libuv` event loop as runtime
execution and game tasks. Process startup, the input timer, and shutdown
ordering are described separately in
[Event Loop and Process Lifecycle](event-loop.md).

## Ownership and Threading

`NetworkRuntime` is the network subsystem's ownership boundary. It owns:

- the connection-slot array;
- per-connection Telnet state;
- input and output buffers;
- accepted client handles while they are live;
- pending asynchronous write requests;
- connection state and fair-poll position.

The `libuv` loop and listener-handle storage are borrowed from `sin`. They must
remain alive until network shutdown has been requested and all `libuv` close
callbacks have drained.

Network state is not synchronized for concurrent access. Creation, polling,
writes, disconnects, callbacks, shutdown, and destruction all belong on the
owning event-loop thread.

`network_runtime_destroy()` is deliberately fail-closed. It will not free the
runtime while listener state, client handles, or asynchronous writes remain
live. Shutdown and destruction are therefore separate operations:
`network_runtime_shutdown()` begins transport teardown; destruction occurs only
after the event loop has delivered the resulting callbacks.

## Connection Lifecycle

Each connection occupies one slot in the runtime. Internally, a line moves
through these logical states:

```text
empty
  -> connecting
  -> idle <-> data
  -> disconnecting
  -> empty
```

`empty` means that the slot is reusable. Reusability requires more than the
absence of a connected socket: the client handle, Telnet object, input/output
buffers, in-flight write state, close state, and pending disconnect event must
all have been retired.

A newly accepted connection enters `connecting`. The next successful network
poll reports its connect event and moves it to `idle`.

Incoming Telnet data is decoded into the line's input buffer. A complete input
line makes the slot eligible for a data event; after all complete lines have
been consumed it returns to `idle`.

A local disconnect request or transport failure moves the slot to
`disconnecting`. The `libuv` handle is closed asynchronously. During normal
operation, the slot remains non-reusable until the close callback has run,
outstanding write state has been released, and `network_runtime_poll()` has
delivered the disconnect event. Process shutdown may retire closed line state
directly once no transport callbacks or writes remain.

This separation is intentional. A connection disappearing from the transport
does not make its line number immediately available for another client.

## Input and Fair Polling

`network_runtime_poll()` is the bridge between asynchronous network activity
and Sinistra code. Each call examines the connection slots from a rotating
cursor and returns at most one event:

- `NETWORK_EVENT_CONNECT`;
- `NETWORK_EVENT_DISCONNECT`;
- `NETWORK_EVENT_DATA`; or
- `NETWORK_EVENT_NONE`.

The cursor advances between calls rather than beginning repeatedly at line
zero. Busy connections therefore cannot indefinitely hide events on later
slots.

For a data event, the runtime extracts one complete decoded input line. The
returned input string is newly allocated and becomes the caller's responsibility
to free.

Input buffering is bounded. Both the total unread input and the length of an
individual unterminated line have enforced limits. Invalid buffer state,
allocation failure, or input exceeding those limits causes the affected
connection to be disconnected rather than allowing unbounded growth.

The process input item normally calls the public `net.input` libcall, which in
turn exposes this one-event polling model to Sinistra code. The repeating input
timer and its scheduling guarantees belong to the
[event-loop documentation](event-loop.md).

## Output, Flushing, and Backpressure

Network output is asynchronous.

`network_runtime_write()` passes application text through the Telnet layer and
queues the resulting transport bytes. Acceptance by this function means that
the output entered the network subsystem; it does not mean that the peer has
received it.

Queued output and output already submitted to `libuv` are accounted separately.
When a flush starts a write, the bytes for that request are copied into storage
owned by the write request. That storage remains untouched until `libuv` invokes
the completion callback.

Only one write is in flight for a line at a time. Further output may accumulate
in the queued buffer while that write is pending. Queue size, in-flight size,
and total pending output are bounded. Persistent inability to make progress is
also bounded: excessive backpressure disconnects the line rather than permitting
unbounded memory consumption.

`network_runtime_flush()` starts delivery where possible;
`network_runtime_flush_all()` does the same for all active lines. Normal process
input processing flushes all network output after executing the configured
input item.

A requested local disconnect normally attempts to drain already queued output
before closing the connection. Once draining is complete, or if transport
failure makes draining impossible, the client handle is closed asynchronously.

## Telnet Integration

`network.c` owns Sinistra's use of Telnet and `libuv`. `libtelnet.c` and
`libtelnet.h` are integrated libtelnet 0.23 sources and should remain generic
protocol-engine code rather than acquiring Sinistra connection policy.

Incoming socket bytes are passed to libtelnet. Telnet data events feed the
Sinistra input buffer; Telnet send events feed the network output queue.
Unrecoverable Telnet errors request disconnection of the affected line.

Sinistra uses libtelnet's NVT end-of-line handling and negotiates Telnet
options through libtelnet's state machine.

Telnet negotiation is stateful. In particular, option negotiation follows the
RFC 1143 model: requesting a state change does not imply that a particular
command byte will be emitted immediately. Depending on the existing
negotiation state, a request may be suppressed, queued, or emitted later.
Tests and maintenance work must therefore reason about state transitions rather
than individual negotiation bytes in isolation.

Echo control is implemented through this negotiation boundary; callers should
not bypass libtelnet by constructing Telnet command sequences directly.

## Public Network Boundary

Production callers see `NetworkRuntime` as an opaque type through
`src/net/network.h`.

The network API owns transport operations such as:

- listener creation and shutdown;
- polling network events;
- writing and flushing output;
- requesting disconnect;
- Telnet echo negotiation;
- connection-state queries;
- copying a peer address.

Sinistra-visible behaviour belongs in the corresponding `net.*` libcalls.
Those handlers translate between runtime values and the network API; they
should not manipulate connection slots, `libuv` handles, Telnet objects, or
transport buffers directly.

Conversely, `network.c` should not acquire language-level policy which belongs
in a libcall. Its job is to maintain a safe asynchronous transport abstraction.

`input_processor()` is the deliberate integration crossing: it is a `libuv`
timer callback which runs the configured Sinistra input item and then flushes
network output. The process lifecycle and ownership of that timer are
documented in [Event Loop and Process Lifecycle](event-loop.md).

## Listening and Accepted Handles

`NetworkRuntime` borrows listener storage supplied by process startup and tracks
whether those listeners have been initialized. The listener setup supports IPv6
with explicit IPv4 support/fallback.

Accepted client handles are allocated for individual connections and remain
owned by the network lifecycle until their `libuv` close callbacks run. A client
handle must never be freed merely because close has been requested: `libuv`
owns the live handle until the close callback.

Failure while accepting or initializing a connection follows the same rule.
Partially initialized transport state is either retired synchronously where
safe or handed to `uv_close()` and released by its callback.

## Shutdown Invariants

Network shutdown must preserve these invariants:

- borrowed loop and listener storage outlive all callbacks which use them;
- no connection slot is reused while transport or disconnect state remains;
- no client handle is freed before its `libuv` close callback;
- no asynchronous write buffer is freed or reused before its write callback;
- queued and in-flight output remain bounded;
- each poll exposes at most one network event and preserves fair rotation;
- `network_runtime_destroy()` does not claim success while live transport
  resources remain.

These rules are more important than the particular internal representation of a
line or buffer. Refactoring the network subsystem should preserve them even if
the state machine or storage layout changes.

## Maintenance and Tests

The production network boundary is implemented in `src/net/network.c` and
`src/net/network.h`. The integrated Telnet engine is in
`src/net/libtelnet.c` and `src/net/libtelnet.h`.

Network coverage includes isolated connection-state, buffering, negotiation,
failure and ownership tests together with localhost chat integration. Public
`net.*` behaviour is also covered at the libcall boundary.

Changes should be tested at the boundary they affect:

- connection lifecycle and transport ownership changes need network
  state-machine coverage;
- Telnet changes need negotiation-transition coverage, not merely expected byte
  sequences;
- `net.*` behaviour changes need the corresponding libcall coverage;
- callback, shutdown, or handle-lifetime changes should retain end-to-end
  event-loop coverage.
