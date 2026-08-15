## Tasks ##

An important concept to remember when writing Sinistra code is *no perpetual loops, ever*. The engine is built around a run-loop, which responds to network events, input callbacks, and timer tasks. The input timer has a nominal 10ms interval, but eligibility is not a real-time guarantee: other callbacks or a long-running input item can delay it. Each `net.input` call handles at most one pending connection, disconnection, or complete-line event, so queued events are handled over later fair-queue turns.

`task.newgametask{item, start, repeat}` uses integer intervals in tenths of a
second, converted to timer milliseconds. `start` is the delay before the first
callback and `repeat` is the interval between later callbacks. A zero repeat
interval makes the task one-shot; it retires automatically after its callback,
including the `start = 0, repeat = 0` immediate case. A positive repeat
interval keeps the task active until `task.killtask{id}` closes it. Intervals
must be non-negative integers no greater than `INT64_MAX / 100`; a task also
requires an initialized event loop and an existing item, and logs an error rather than executing when that item is not a code item.

Within a timer task callback, `task.thisid` reports that task's id, including
while synchronously called items execute and after the task asks to close itself.

Outside such a callback it returns `nil`. `task.exists{id}` checks whether an id
is currently scheduled, and `task.count` reports how many scheduled tasks remain; a close request makes both observations change immediately.

