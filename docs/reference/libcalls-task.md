# The task library

[Reference Manual](README.md) · [Libcall index](libcalls.md) ·
[Error Reference](errors.md)

Task management is handled here.

| Libcall | Arguments | Returns | Side effects | Failure behaviour | Example |
| --- | --- | --- | --- | --- | --- |
| `task.newgametask{item, start, repeat}` | `item` must evaluate to either the string name of an existing item or an item reference. String paths resolve relatively where applicable and both forms are canonicalized with ASCII case folding before lookup and scheduling; ordinary string payloads are unchanged. `start` and `repeat` must be integers; floats are invalid for both intervals. Negative intervals and intervals above `INT64_MAX / 100` are rejected before scheduling because they cannot be safely converted to timer milliseconds. The integer intervals are interpreted in tenths of a second and converted to milliseconds before scheduling; any `repeat` of `0` is one-shot, while `start = 0, repeat = 0` is scheduled with zero delay and runs on the next eligible event-loop turn, never synchronously in this call. The item name or item reference and both intervals are popped and released exactly once on every path; the scheduled task owns its copied canonical item name. | The integer task id on success; `nil` when arguments are invalid, the item cannot be found, or the timer cannot be created. | Creates a timer-backed game task that attempts to execute the named item after `start` and then repeats every `repeat` interval when `repeat` is non-zero. One-shot tasks retire automatically after their callback; repeating tasks remain active until killed. Each firing resolves the stored canonical path afresh. The task callback only executes the item when it is a code item; a non-code item logs an error when the task fires. | Invalid argument types set the runtime invalid-arguments error and return `nil`. A missing item sets the no-such-item error and returns `nil`. Negative intervals, intervals above `INT64_MAX / 100`, or timer setup failures set the runtime invalid-arguments error and return `nil`. | `@id = task.newgametask{"heartbeat", 10, 50};` |
| `task.killtask{id}` | `id` must evaluate to an integer task id; floats are invalid. | `true` when a task with that id is found and closed; `false` when no task with that id exists or `id` is negative; `nil` for invalid argument types. | Closes the timer for the matching task. | Invalid argument types set the runtime invalid-arguments error and return `nil`.  Unknown or negative task ids return `false` without setting an error. | `task.killtask{@id};` |
| `task.thisid` | None. | The positive integer id of the timer-backed task currently executing, or `nil` outside a task callback. | The callback identity remains available to synchronously called items and after that task requests its own close, until the callback returns. A new task receives its own identity rather than its creator's. | Both valid outcomes preserve unrelated `error`. | `@id = task.thisid;` |
| `task.exists{id}` | `id` must evaluate to an integer task id. | `true` when the id currently resolves to a scheduled task; otherwise `false`, including negative and unknown ids. | Checks the current task lookup without modifying it. A close request removes a task from lookup immediately. | Non-integer arguments, including floats and strings, set `ERR_RUNTIME_INVALIDARGS` with task-specific detail and return `nil`. Valid boolean outcomes preserve unrelated `error`. | `if task.exists{@id} then ...; endif;` |
| `task.count` | None. | Number of tasks currently present in lookup as a non-negative integer. | Excludes tasks removed by a close request; returns `0` when none are scheduled. | Success preserves unrelated `error`; a theoretical signed-integer overflow saturates at `INT64_MAX`. | `@scheduled = task.count;` |

## Lifecycle

Tasks and task IDs are runtime-only. Saving or loading an itemstore,
including through `sys.save`, does not serialize or recreate scheduled tasks.
Initializing the task subsystem resets its registry and ID allocator. IDs are
ephemeral and may recur after a task retires or after subsystem restart.

Scheduling stores a copied canonical target path; it does not pin or snapshot
the target. Each timer firing resolves that path again, so replacement,
deletion, and later recreation take effect on subsequent firings. Only active
callback execution pins the resolved target, rejecting replacement or deletion
until the callback returns.
