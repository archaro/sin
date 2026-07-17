# Runtime ownership and API boundaries

This page summarizes the ownership contracts between the interpreter runtime,
the itemstore, and native library-call handlers.

## Runtime context and interpreter

`RuntimeContext` is the execution context passed to opcode and libcall handlers.
It borrows process-level dependencies such as the VM, itemstore root, libuv loop,
configuration strings, network state, and shutdown flag. Runtime execution may
mutate those dependencies, but the context does not own or free them.

The context owns only its per-invocation interpreter bookkeeping, such as the
runtime decoder, current item pointer, pending call item pointer, opcode table,
and interpreter-initialization flag. The bytecode and items referenced by that
bookkeeping remain owned by the itemstore or by the caller that supplied them.

`interpret(ctx, item)` returns a `VALUE_t` by value. The caller owns that returned
value and must free any string payload in it when the value is no longer needed.
The interpreter borrows and mutates `ctx->vm`: it uses the existing VM stack and
call stack, pushes and pops values while executing, and returns the top-level
return value after popping it from the stack. It does not create an isolated VM
stack for the call.

During execution, the interpreter sets the executing code item's `inuse` flag to
`true` and clears it when that top-level frame exits or aborts. Internal calls
between code items update `inuse` as frames are entered and returned. The flag is
used to prevent deletion or replacement of running code; it is not an ownership
claim on the item.

Nested calls to `interpret` with the same `RuntimeContext` are supported for
runtime code paths that need to execute temporary or secondary code: the
interpreter saves and restores decoder/current-item state around each call. The
VM stack and call stack are still shared, so nested callers must account for the
stack effects of both the nested code and its returned `VALUE_t`.

## Code item result semantics

A code item's result is the value left on top of the VM stack when that code
item halts, above that frame's locals and parameters. `interpret()` pops that
value and returns it to the caller, then discards the frame's locals and
parameters before returning or resuming the caller. If the code item halts with
no result value above its frame, the result is `nil`.

At the language level, the compiler preserves only the final top-level
expression statement as the code item result. Expression statements before the
final top-level statement are compiled with `DISCARD`, so their values are
evaluated and then removed from the stack.

Statement forms such as assignment, `if`, and `while` do not themselves produce
a result value, and local variables do not leak out as implicit results.
Expression statements inside `if` branches or `while` bodies are also
discarded; they do not become the enclosing code item's result merely because
the branch or loop is the last top-level statement. To return a value after a
branch or loop, place the desired expression in the following final top-level
expression statement.

Libcalls and item calls follow the same expression-statement rule as other
expressions. If a libcall such as `sys.exists{"name"}` or `sys.compile{source}`
is the final top-level expression statement, its return value is the code item
result. If it appears earlier, its return value is discarded after any side
effects have occurred.

## Itemstore ownership

The item tree owns all `ITEM_t` nodes reachable from the root. Parent and child
links are borrowed references within that tree; `destroy_item` recursively frees
an item, its children, its child hash table, its ordered child-pointer array, its
owned code bytecode buffer, and any owned string payload in a value item.

Value items own their stored `VALUE_t` payload. When `make_item`, `insert_item`,
or `set_item` stores a string `VALUE_t`, ownership of the string buffer transfers
to the itemstore. Callers must not free or reuse that string after a successful
store. Replacing a value item frees the previous owned payload.

Code items own their bytecode buffers. `make_item` takes ownership of the
bytecode pointer for code items, and `insert_code_item` takes ownership of the
bytecode pointer when it successfully installs it. Callers retain ownership on
validation failure or other failure before installation.

`get_itemfilename` allocates and returns a path string for the caller to free.
`save_itemsource` borrows both the item and source text only for the duration of
the call. `load_itemstore` returns a newly allocated item tree on success; the
caller owns that root and must release it with `destroy_item`.

## Runtime diagnostics options

`--strict-validation` enables bytecode verification before runtime execution of
code items and while loading itemstores. It only checks whether encoded bytecode
is structurally valid for execution; failures are reported as
`ERR_RUNTIME_BYTECODE` (or reject the persisted code item during load).

`--strict-runtime-contracts` is a separate diagnostic mode for runtime argument
contracts. In default mode, item fetch/call execution may intentionally discard
supplied arguments when a code item receives too many arguments, when the target
item is missing, or when the computed item name is invalid. This is a live-update
design choice: code can keep running while callers and callees are updated to
match a changed parameter list. With strict runtime contracts enabled, those
stack effects and return values are preserved, but the interpreter performs
extra checks, sets `error` to `ERR_RUNTIME_INVALIDARGS`, writes a detail string
to `error.msg`, and logs a runtime contract violation. Enabling
`--strict-validation` alone does not enable these dropped-argument diagnostics;
use `--strict-runtime-contracts` when you want runtime contract reporting and can
accept the extra runtime overhead. For example, `add{1, 2, 3}` and
`missing.item{1}` keep their normal return values, but strict runtime contracts
also set `ERR_RUNTIME_INVALIDARGS` and describe the discarded argument in
`error.msg`.

## Libcall API boundary

Libcall handlers use the same opcode-handler signature as bytecode opcodes. The
compiler emits bytecode that evaluates libcall arguments first, so handlers find
their arguments on the VM stack. Each handler is responsible for consuming the
number of arguments declared in the libcall registry and for leaving exactly one
return value on the stack before returning `nextop`.

Arguments popped from the stack are owned by the handler. If a popped argument is
a string and the handler does not transfer it into the itemstore or return it to
Sinistra code, the handler must free it. Arguments left in place, such as the
string-mutating `str.*` calls, remain owned by the stack and become the return
value.

A libcall should push `VALUE_NIL`, `VALUE_FALSE`, or another explicit value for
failure cases that are visible to Sinistra code. Returning `NULL` from a handler
is reserved for fatal interpreter/opcode failures and causes interpretation to
abort with `nil`.

Most invalid libcall argument types, ranges, or values use a shared runtime
policy: the handler consumes and frees its arguments, sets `error` to
`ERR_RUNTIME_INVALIDARGS`, sets `error.msg` to a handler-specific diagnostic when
available, and pushes the documented failure value for that libcall. Handlers
should use the helpers in `src/libcall/libcall_common.h`, such as
`lc_invalid_args_return` or `lc_invalid_args_detail_return`, so the error item
and stack result are updated consistently. Domain failures that are not invalid
arguments, such as missing items, unknown task ids, inactive network lines, or
compiler diagnostics from valid `sys.compile` source strings, continue to use
their own documented errors or non-error return values.

Runtime errors also set `error.item` to the full name of the code item executing
when the error was reported. Compiler diagnostics clear `error.item` to `nil`
because they describe source text rather than the currently executing item.

## Network line lifecycle

Network slots move through a small logical lifecycle:

- Active: `LINE_connecting`, `LINE_idle`, and `LINE_data` own a live connection
  slot. `net.write` only writes to the writable active states, `LINE_idle` and
  `LINE_data`, after Telnet setup has completed.
- Disconnecting: `LINE_disconnecting` means the connection has been asked to
  close or libuv has reported remote closure. Repeated disconnect requests are
  no-ops, writes are ignored by `net.write`, and pending output may drain before
  the handle is closed when the disconnect was requested locally.
- Disconnected/reusable: `LINE_empty` with no handle, Telnet object, buffers, or
  pending output state is reusable. `net.input` reports the disconnect event,
  destroys the line, and returns the slot to this reusable state.
