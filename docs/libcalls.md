# Sinistra Library Calls

This reference is derived from the registered `libcalls[]` table in
`src/libcall_table.c`.  Library calls are pseudo-items of the form
`library.call{arguments}` and always push a return value.  Calls with arity `0`
are written without an argument list in normal Sinistra source, for example
`net.input`.


## Invalid argument policy

When a libcall receives an argument with an invalid type, range, or value, it
consumes the argument values, sets `error` to `ERR_RUNTIME_INVALIDARGS`, sets
`error.msg` to a libcall-specific diagnostic, and returns that libcall's
documented invalid-argument value. Current invalid-argument return shapes are
`false` for `sys.compile{source}` and `nil` for `task.newgametask`,
`task.killtask`, `net.write`, `str.capitalise`, `str.upper`, and `str.lower`.
Failures that are not invalid arguments keep their own contract; for example a
missing task item sets the no-such-item error, an unknown task id returns
`false` without changing `error`, and an inactive network line returns `nil`
without changing `error`.



Examples:

* `task.newgametask{"heartbeat", -1, 10};` returns `nil`, sets `error` to
  `ERR_RUNTIME_INVALIDARGS`, and writes an interval-range diagnostic to
  `error.msg`.
* `str.upper{42};` returns `nil`, sets `ERR_RUNTIME_INVALIDARGS`, and reports
  that the string argument is invalid.
* `task.killtask{999999};` returns `false` without changing `error` because an
  unknown task id is a domain miss, not an invalid argument.

## Registered libcalls

| Libcall | Library | Call | Arity | Argument expectations | Return value | Side effects | Failure behaviour | Example |
| --- | --- | --- | ---: | --- | --- | --- | --- | --- |
| `sys.backup` | `sys` | `backup` | 0 | None. | `nil`. | Saves a backup copy of the current in-memory itemstore.  The filename is the configured itemstore name followed by a timestamp suffix. | The call does not report backup errors through its return value, but failures are written to the error log. | `sys.backup;` |
| `sys.log{value}` | `sys` | `log` | 1 | Any expression.  Strings are logged as text, integers as decimal integers, floats as canonical binary64 decimal text (including `0.0`, `-0.0`, `inf`, `-inf`, and `nan`), booleans as `true` or `false`, and `nil` is ignored. | `nil`. | Writes to the system log. | Unknown value types produce a diagnostic log message. | `sys.log{"player connected"};` |
| `sys.shutdown` | `sys` | `shutdown` | 0 | None. | `nil`. | Logs the request, marks the shutdown as safe, and stops the event loop so the engine can shut down cleanly and save the itemstore. | No failure is reported to Sinistra code. | `sys.shutdown;` |
| `sys.abort` | `sys` | `abort` | 0 | None. | `nil`. | Logs the request, marks the shutdown as unsafe, and stops the event loop without the normal itemstore save. | No failure is reported to Sinistra code. | `sys.abort;` |
| `sys.compile{source}` | `sys` | `compile` | 1 | `source` must evaluate to a string containing Sinistra source code; any non-string value, including a float, is invalid and is not converted to a string. | `true` when the source compiles and executes; `false` when the argument is invalid, compilation fails, or the temporary code item cannot be created. | Compiles the source into a temporary code item, executes it, discards extra stack values produced by that execution, deletes the temporary item, and clears `error` / `error.msg` on success.  Any side effects caused by the compiled Sinistra code still occur. | Non-string input logs a message, sets the runtime invalid-arguments error, and returns `false`.  Compile or temporary-item failures set the corresponding error item and return `false`. | `@ok = sys.compile{"foo = 42;"};` |
| `task.newgametask{item, start, repeat}` | `task` | `newgametask` | 3 | `item` must evaluate to the string name of an existing item.  `start` and `repeat` must be integers; floats are invalid for both intervals.  Negative intervals and intervals above `INT64_MAX / 100` are rejected before scheduling because they cannot be safely converted to timer milliseconds.  The integer intervals are interpreted in tenths of a second and converted to milliseconds before scheduling; `0, 0` schedules one immediate, non-repeating execution. | The integer task id on success; `nil` when arguments are invalid or the item cannot be found. | Creates a timer-backed game task that executes the named code item after `start` and then repeats every `repeat` interval when `repeat` is non-zero. | Invalid argument types set the runtime invalid-arguments error and return `nil`.  A missing item sets the no-such-item error and returns `nil`.  Negative intervals or intervals above `INT64_MAX / 100` set the runtime invalid-arguments error and return `nil`. | `@id = task.newgametask{"heartbeat", 10, 50};` |
| `task.killtask{id}` | `task` | `killtask` | 1 | `id` must evaluate to an integer task id; floats are invalid. | `true` when a task with that id is found and closed; `false` when no task with that id exists; `nil` for invalid argument types. | Closes the timer for the matching task. | Invalid argument types set the runtime invalid-arguments error and return `nil`.  Unknown task ids return `false` without setting an error. | `task.killtask{@id};` |
| `net.input` | `net` | `input` | 0 | None. | Integer activity code: `0` for no activity, `1` for a new connection, `2` for a disconnection, or `3` for received input data. | Advances the fair-queue connection cursor.  For connection, disconnection, and data events, sets the configured input line item, normally `input.line`.  For data events, also stores the received line of text in the configured input text item, normally `input.text`.  A disconnection event destroys the line before returning. | No failure is reported to Sinistra code; no activity returns `0`. | `@event = net.input;` |
| `net.write{line, value}` | `net` | `write` | 2 | `line` must evaluate to an integer connection index in range; floats are invalid for `line`.  `value` may be a string, integer, float, boolean, or `nil`; `nil` writes nothing. | `nil`. | Sends text to the selected active telnet connection.  Integers, floats, and booleans are converted to text before sending; float formatting matches `sys.log`, including signed zero and non-finite values. | An invalid line argument sets the runtime invalid-arguments error and returns `nil`.  If the line is in range but not currently writable, the call returns `nil` without setting an error. | `net.write{input.line, "Hello!\n"};` |
| `str.capitalise{text}` | `str` | `capitalise` | 1 | `text` must evaluate to a string; non-string values are invalid. | The same string value with its first byte uppercased; `nil` for invalid non-string input. | Mutates the string value on top of the VM stack. | Non-string input sets the runtime invalid-arguments error and returns `nil`. | `@name = str.capitalise{"sinistra"};` |
| `str.upper{text}` | `str` | `upper` | 1 | `text` must evaluate to a string; non-string values are invalid. | The same string value with all bytes uppercased; `nil` for invalid non-string input. | Mutates the string value on top of the VM stack. | Non-string input sets the runtime invalid-arguments error and returns `nil`. | `@shout = str.upper{"hello"};` |
| `str.lower{text}` | `str` | `lower` | 1 | `text` must evaluate to a string; non-string values are invalid. | The same string value with all bytes lowercased; `nil` for invalid non-string input. | Mutates the string value on top of the VM stack. | Non-string input sets the runtime invalid-arguments error and returns `nil`. | `@quiet = str.lower{"LOUD"};` |
