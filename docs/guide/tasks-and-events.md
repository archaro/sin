# Tasks and Events

Sinistra can schedule code items to run once or repeatedly using the `task`
library. Tasks run on the same event loop as the rest of the world: they are
not threads, and task code executes synchronously once its callback begins. A
task should therefore do its work and return promptly.

For the relationship between tasks, network events, the `input` item, and the
event loop, see the [Runtime Model](runtime-model.md).

Remember that Sinistra is not a RTOS.  When scheduling tasks, the timer is
a suggestion to the event loop, not a guarantee of periodicity.

## Creating a Task
`task.newgametask{item, start, repeat}` uses integer intervals in tenths of a
second.
- `item` is either the item name as a string or an item reference to the item
  which is being scheduled. String paths and references are resolved with
  ASCII case folding and stored as canonical lower-case paths; this does not
  change ordinary string values.
- `start` is the delay before the first callback
- `repeat` is the interval between later callbacks.
When calling `task.newgametask`, both `start` and `repeat` must be non-negative.

If the call to `task.newgametask` is successful, it returns a non-zero integer.
This is the task id, and can be used to refer to the task later.
```sinistra
@id = task.newgametask{&heartbeat, 10, 50};
```

It is worth noting that when a task is scheduled, the path of the target item
is stored, not a frozen copy of the item as it existed when the task was
created. The target code item may therefore be replaced between runs, and the
new version will execute next time the task fires. It may also be deleted; if
it is later recreated at the same path, the task will resolve that path afresh.
Scheduling does not pin the target. Only the target resolved for an actively
executing callback is pinned, so that callback cannot replace or delete itself;
the target may be replaced, deleted, or recreated between callbacks.

## Repeating and One-shot Tasks
A zero repeat interval makes the task one-shot; it retires automatically after
its callback. Setting both `start` and `repeat` to `0` schedules the task at
zero delay, and the callback runs on a later eligible event-loop turn, never
synchronously inside `task.newgametask`. A positive `repeat` interval keeps the
task active until it is cancelled.

## Cancelling a Task
`task.killtask{id}` takes an integer argument.  If a task is found with a
matching id, that task is descheduled.  Tasks may terminate themselves by
calling `task.killtask` with their own id (obtained by calling `task.thisid`).
The task will run to its natural completion, but will be removed from the event
loop.

## The Current Task
The id is assigned when the task is created and does not change. While a task
callback is executing, task.thisid continues to return that id even if the task
has asked to cancel itself; it returns nil once execution has returned to an
ordinary non-task context.

Tasks and IDs are runtime-only and ephemeral: itemstore save/load, including
`sys.save`, does not persist or recreate them. Initializing the task subsystem
clears its registry and resets ID allocation, and a retired or restarted task
ID may be reused.

## Inspecting Scheduled Tasks
- `task.exists{id}`: returns `true` if a task with that id is currently
scheduled, otherwise `false`.
- `task.count` reports how many scheduled tasks remain.
