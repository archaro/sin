# Networking guidance

These instructions supplement the repository-root `AGENTS.md` for `src/net/`.

- `libtelnet.c` and `libtelnet.h` are integrated libtelnet 0.23 sources. Preserve
  their local style and avoid unrelated cleanup or Sinistra-specific behavior
  there. Change them only when the protocol engine, its public API, or compiler
  portability genuinely requires it.
- Keep Sinistra connection policy and libuv integration in `network.c` and
  `network.h`; put public `net.*` behavior in the corresponding libcall.
- Telnet option negotiation is stateful and follows RFC 1143. A requested
  negotiation can be suppressed, queued, or emitted later depending on the
  existing option state, so test transitions as well as individual bytes.
- Preserve nonblocking behavior. Keep ownership and lifetime of libuv handles,
  callbacks, connection buffers, and close paths explicit.

Choose validation at the affected boundary:

- `make test-network` for networking and protocol behavior.
- The focused cases in `tests/core/test_libcall_net.c` for `net.*` argument,
  state, and negotiation-byte behavior.
- `make test-chat-smoke` for end-to-end client/server behavior.
