# Event loop and process lifecycle

`sin` owns the libuv loop and stages startup so every borrowed dependency is
alive until callbacks have drained. The process is single-threaded: runtime,
itemstore, task, and network mutation belongs to the loop thread.

## Startup contexts

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

## Tasks and callback lifetime

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

## Network ownership and fair polling

`NetworkRuntime` owns its connection slots, Telnet state, input/output buffers,
and per-connection transport state. The loop and listener storage are borrowed
from `sin` and must outlive `network_runtime_destroy()`. A line progresses
through connecting, idle/data, disconnecting, and empty/reusable states. Telnet
events append decoded data to the input queue or encoded bytes to the output
queue. Input lines are bounded; output has bounded queued and in-flight
capacity, and excessive backpressure requests disconnect.

The input callback runs the configured input item once, and the input item uses
`network_runtime_poll()` to process at most one event per call. Poll advances a
rotating cursor, so connect/disconnect/data events receive fair service across
the configured slots. Writes copy data into a libuv request; the output buffer
is not reused until its write callback completes. A requested disconnect drains
queued output when possible, then closes the client handle. `uv_close` callbacks
release the handle; the subsequent disconnect event must also be delivered and
line resources released before a slot is reusable. All transport and Telnet
state must be gone before runtime destruction.

## Shutdown and partial failure

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

## Maintenance and tests

Process staging and cleanup are in `src/sin.c`; task states and close draining
are in `src/runtime/task.c` and `task.h`; network state, queues, Telnet
integration, and fair polling are in `src/net/network.c` and `network.h`.
The network-specific guidance is in [`src/net/AGENTS.md`](../../src/net/AGENTS.md).
Focused coverage is in `tests/core/test_task_lifecycle.c`,
`tests/network/test_network.c`,
`tests/network/test_chat_smoke.c`, `test_sin_itemstore_policy.c`, and the
`tests/rewrite` task/network adapters. Changes to callback ownership or
shutdown should retain both isolated state-machine checks and end-to-end loop
coverage.
