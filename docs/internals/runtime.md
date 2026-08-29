# Runtime Ownership and API Boundaries

This page summarizes the ownership contracts between the interpreter runtime,
the itemstore, and native library-call handlers.

## Runtime Boundary

Runtime execution is hosted by the single-threaded `libuv` process lifecycle
described in [Event Loop and Process Lifecycle](event-loop.md). This page
begins at the point where a `RuntimeContext` and VM are available to execute
code; startup, task scheduling, network polling, and shutdown ownership are
documented there.

## Runtime Context and Interpreter

`RuntimeContext` is the execution context passed to opcode and libcall
handlers. It borrows process-level dependencies such as the VM, itemstore,
`libuv` loop, configuration, network state, and shutdown state, but owns its
runtime-local state: interpreter bookkeeping, opcode dispatch table, libcall
registry, and bytecode-verification cache. Runtime execution may mutate
borrowed dependencies but does not own or free them. Bytecode and `ITEM_t`
objects referenced by the context remain borrowed from the itemstore or caller.

All multi-step frame changes go through the checked `runtime_frame` boundary.
It captures the VM checkpoint for an invocation, validates stack/call-stack
capacity before normalizing arguments or publishing a continuation, and owns
the execution pin for each frame it enters. Return and unwind operations restore
the checkpoint and release exactly the pins owned by that invocation, including
the pending callee transfer on verification failure.

`interpret(ctx, item)` returns a `VALUE_t` by value. The caller owns the whole
returned value and must call `value_free(&result)` when it is no longer needed;
this releases owned string, list, and item-reference payloads as applicable.
The interpreter borrows and mutates `ctx->vm`: it uses the existing VM stack and
call stack, pushes and pops values while executing, and returns the top-level
return value after popping it from the stack. It does not create an isolated VM
stack for the call.

During execution, each active interpreter frame owns one transient execution pin
on its code item. A suspended caller remains pinned while a callee runs; return
releases only the callee pin, while abort releases the current frame and all
caller frames created by that invocation. Any nonzero pin prevents replacement,
and deletion checks the entire target subtree before detaching anything. Pins
are not serialized and are not an ownership claim on the item.

Nested calls to `interpret` with the same `RuntimeContext` are supported for
runtime code paths that need to execute temporary or secondary code: the
interpreter saves and restores decoder/current-item state around each call. The
VM stack and call stack are still shared, so nested callers must account for the
stack effects of both the nested code and its returned `VALUE_t`.

## Code Item Result Semantics

Each invocation has an explicit frame result. An explicit `RETURN` expression
transfers its owned value to the caller; fallthrough and valueless termination
yield `nil`. Frame cleanup discards locals, parameters, and residual stack
values without leaking ownership.

## Itemstore Ownership

`RuntimeContext` borrows an `ITEMSTORE_t`. The store owns items, stored values,
and bytecode; runtime item pointers are therefore non-owning and may not
outlive their store or a deleted containing subtree. Successful item mutations
transfer supplied payload ownership according to the itemstore API.

Runtime values returned from itemstore operations are cloned or otherwise given
explicit `VALUE_t` ownership before they are placed on the VM stack. Item
references contain canonical paths rather than borrowed `ITEM_t *` pointers and
resolve against the current store when used.

See [Itemstore Operations](itemstore.md) for mutation, pinning, revision,
persistence, error-publication and sidecar ownership rules.

## Runtime Diagnostics Options

Executable code is always verified for memory-safe structure (including stack
flow, local indices, and jump targets) before runtime execution; failures are
reported as `ERR_RUNTIME_BYTECODE`. The runtime uses the no-options
`bc_verify_executable_bytecode()` API for this mandatory check, so no runtime
configuration can weaken it. `--strict-validation` additionally applies the
same verification while loading itemstores, rejecting malformed persisted code
items before they enter the store.

`--strict-runtime-contracts` is a separate diagnostic mode for otherwise
tolerated runtime call-contract mismatches. It preserves the normal stack
effects and return values but additionally publishes `ERR_RUNTIME_INVALIDARGS`
diagnostics. It is diagnostic instrumentation rather than a stricter execution
model; see the [Reference Manual](../reference/README.md) for the observable
cases.

## Libcall API Boundary

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

Ordinary Sinistra-level failures must still leave one explicit result on the VM
stack; returning `NULL` is reserved for failures which abort interpretation.
Shared invalid-argument handling should use the helpers in `libcall_common.h`
so argument cleanup, error publication and result placement remain consistent.

Runtime bytecode-shape failures use `ERR_RUNTIME_BYTECODE`, invalid item
mutations use `ERR_RUNTIME_INVALIDITEM`, and internal invariant failures use
`ERR_RUNTIME_INTERNAL`. User-visible per-libcall failure semantics belong in
the [Reference Manual](../reference/README.md). Runtime diagnostics associate
`error.item` with the executing code item where applicable.

## Network Boundary

`RuntimeContext` borrows an opaque `NetworkRuntime *` for libcalls that require
network services. Connection and `libuv` ownership does not belong to the
interpreter; see [Event Loop and Process Lifecycle](event-loop.md).

## Runtime Bytecode Verification Cache

Each `RuntimeContext` owns a small fixed-size cache of successful
executable-bytecode verification results. Entries identify the bytecode, owning
itemstore, current mutation tokens and verification policy. The context
synchronises the cache against itemstore topology and payload revisions before
reuse, so replacement or deletion cannot make an old successful verification
applicable to new code.

Payload replacement changes the itemstore payload revision. The itemstore also
advances payload and topology epochs whenever their counters wrap; each
revision/epoch pair is a mutation token. The runtime conservatively clears its
verification entries when it observes a different store, topology revision, or
payload revision (including revision wraparound), so deleted or old payloads
cannot be reused. If either token is exhausted, the itemstore marks that token
permanently exhausted and runtime verification caching remains disabled for
that store. Failed verification is never cached and retains the normal
diagnostic, unwind, pin, and stack behavior on each attempt. Cache capacity and
eviction are deterministic and independent of call count.
