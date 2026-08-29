# Event Loop and Process Lifecycle

`sin` owns the libuv loop and stages startup so every borrowed dependency is
alive until callbacks have drained. The process is single-threaded: runtime,
itemstore, task, and network mutation belongs to the loop thread.

## Startup Contexts

Startup parses options, ensures the source root and persistent itemstore, then
initializes the task registry and loop. Bootstrap bytecode is transferred to a
dedicated boot itemstore (`itemstore_create_boot`) and run by a boot VM/context;
that constructor takes ownership of the supplied byte buffer on success. The
boot item is owned by that temporary store; the boot context uses the main
persistent store for its runtime services and is destroyed after successful
bootstrap (or before a retry after an interrupt). Bootstrap failure does not
publish a partially initialized runtime.

Normal operation creates a separate input VM/context. That context borrows the
main itemstore, loop, configuration strings, and `NetworkRuntime`; it is the
owner of the repeating input callback's interpreter bookkeeping, not those
shared stores or handles. The input timer is started only after the listener
has been established, with one input callback scheduled at a nominal 10 ms
interval.

## Tasks and Callback Lifetime

`TASK_t` objects move through `ALLOCATED -> INITIALIZED -> ACTIVE -> CLOSING ->
DESTROYED`. Each active task owns a VM, runtime context, timer allocation,
task-list node, and a copied canonical target path in its fixed `itemname`
buffer. It owns no code item between callbacks: each timer callback resolves
that path afresh from the current itemstore, and an execution pin exists only
while `interpret()` is running. The task borrows the loop and itemstore. A
one-shot task requests close after execution. Closing unlinks the task and
retires its ID immediately, stops the timer, calls `uv_close`, and defers
VM/context/timer/node destruction to the close callback. `finalise_tasks()`
requests all closes and drains the loop before freeing task-ID storage. No task
object may be freed while its libuv timer callback can still run.

## Network Ownership and Fair Polling

`NetworkRuntime` owns its connection slots, Telnet state, input/output buffers,
fair-poll state, and per-connection transport resources. The libuv loop and
listener storage are borrowed from `sin` and must remain alive until all network
close callbacks have drained and `network_runtime_destroy()` can complete.

The configured input item calls `network_runtime_poll()`, which delivers at most
one pending network event per invocation and advances a rotating cursor so
connections receive fair service. Writes and disconnects are asynchronous:
queued or in-flight transport state remains owned by `NetworkRuntime` until the
corresponding libuv callbacks complete. A connection slot is not reusable until
its transport resources have been released and its disconnect event has been
delivered.

For connection state, transport ownership, Telnet negotiation, and network
buffering, see [Network Internals](network.md).

## Shutdown and Partial Failure

The centralized `shutdown_startup()` path stops the input timer, asks the
network runtime to close listeners and lines, finalizes tasks, closes the timer,
walks remaining loop handles, and runs the loop until close callbacks drain.
Only after that does it destroy the network runtime, input context/VM, and loop.
If a close or loop-close invariant fails, the process reports failure and keeps
storage alive where necessary rather than freeing libuv-owned state. A normal
safe shutdown (including `--loadonly`) saves the main itemstore after runtime
teardown; `sys.shutdown` requests this path. A SIGUSR1 during bootstrap tears
down the boot VM/context and retries bootstrap, while a recovery pending after
bootstrap (including a signal during normal runtime) marks the shutdown unsafe
and skips persistence. `sys.abort` likewise sets `safe_shutdown` false. If
bootstrap never completes, partial startup has no completed runtime to persist
and therefore skips saving independently of the flag's normal safe value.
Bootstrap and startup partial failures clean only resources that were actually
initialized. A failed network destroy with live state is not silently treated
as success.

The key invariants are: callbacks never outlive their owner; borrowed loop,
listener, store, and configuration objects remain valid through callback drain;
task IDs are not reused while a task is still discoverable; and an unsuccessful
save or shutdown does not claim durability.

## Maintenance and Tests

Process staging and cleanup are in `src/sin.c`; task states and close draining
are in `src/runtime/task.c` and `task.h`; network state, queues, Telnet
integration, and fair polling are in `src/net/network.c` and `network.h`.

Focused coverage covers task lifecycle, network state, chat integration, and
shutdown policy coverage. Changes should preserve malformed-input, boundary,
ownership, and failure-atomicity coverage at the affected boundary.
