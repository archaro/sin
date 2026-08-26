# Tasks and Events

Sinistra can schedule code items to run once or repeatedly using the `task`
library. Tasks run on the same event loop as the rest of the world: they are
not threads, and task code executes synchronously. A task should therefore do
its work and return promptly.

For the relationship between tasks, network events, the `input` item, and the
event loop, see the [Runtime Model](runtime-model.md).

Remember that Sinistra is not a RTOS.  When scheduling tasks, the timer is
a suggestion to the event loop, not a guarantee of periodicity.

## Creating a Task

## Repeating and One-shot Tasks
`task.newgametask{item, start, repeat}` uses integer intervals in tenths of a
second.
- `item` is either the item name as a string or a reference to the item which is
being scheduled.
- `start` is the delay before the first callback
- `repeat` is the interval between later callbacks.
A zero or negative repeat interval makes the task one-shot; it retires
automatically after its callback.  Setting both `start` and `repeat` to `0`
also invites the event loop to run the task as soon as it can.
A positive `repeat` interval keeps the task active until it is cancelled.

If the call to `task.newgametask` is successful, it returns a non-zero integer.
This is the task id, and can be used to refer the the task later.

## Cancelling a Task
`task.killtask{id}` takes an integer argument.  If a task is found with a
matching id, that task is removed from the event loop.  Tasks may terminate
themselves by calling `task.killtask` with their own id (obtained by calling
`task.thisid`).  The task will run to its natural completion, but will be
removed from the event loop.

## The Current Task
`task.thisid`, called from within an executing task, returns the id of the task.
It returns `nil` if called from outside a running task.

## Inspecting Scheduled Tasks
- `task.exists{id}`: returns `true` if a task with that id is currently
scheduled, otherwise `false`.
- `task.count` reports how many scheduled tasks remain

