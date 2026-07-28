# How to be Sinister #

The style of the language is something like the misbegotten offspring of Forth and Smalltalk.

## Flow of Operations ##

When the runtime engine starts up, it first loads and executes the bootstrap code (which is separately compiled).  The engine is event-driven and this code sets things up ready for the game to run, including setting up the main game tasks.  Tasks are attached to the runloop and are called as necessary.  There are three kinds:
- Network tasks: the listener, and any player connections created by it.  These tasks run outside the game and interact in limited ways with *Sinistra* code, and their purpose is to manage input from and output to connected players.
- Timer tasks: these are managed by *Sinistra* code (for example, the bootstrap code).  Each time the timer expires, the specified code is run.
- Input task: this is the most important task. `sin` invokes the configured
  input item from a repeating libuv timer with a nominal 10ms interval. The
  callback runs on the event-loop thread, so it is serialized with network and
  other timer callbacks; a busy loop can delay it. The input item should call
  `net.input` to process network activity.

## The Item ##

The fundamental unit in Sinistra is the *item*.  An item can contain many things: integers, floats, strings, Boolean values or `nil`, or it can contain code.  A value item simply returns its value, whereas a code item executes its code and returns the result.  All items return a value (even if the value is `nil`).  Items can also call other items.

String values are byte strings and are capped by `SIN_MAX_STRING_BYTES` in
`src/common/string_limits.h`, currently 65,535 bytes. Source string literals,
persisted string values, and runtime string-building operations fail rather than
constructing a larger value.

Items are nominally hierarchical, although this is only an organisational strategy – there is no inheritance.  Thus the following are items:  
`foo`  
`foo.bar`  
`foo.bar.baz`  
…although there is nothing can be inferred from `foo.bar` by its relationship with `foo`.

Item layer names are strings. Integer layer literals and integer-valued
dereferences are converted to canonical base-10 integer text, so `foo.1` and
`foo.[@i]` with `@i = 1` address the same layer. Float values are not permitted
as layer names or as dereference results used to build a layer name: a literal
such as `foo.1.0` is parsed as layers `foo`, `1`, and `0`, not as a float layer,
and a runtime dereference that evaluates to a float makes item assembly fail and
produce `nil`. Consequently `1`, `1.0`, and `1.00` do not have three
float-derived item-name spellings: only integer `1` is supported as a numeric
layer value, while the dotted forms are normal multi-layer string names if
written explicitly. Likewise `+0.0` and `-0.0` have no item-name mapping, and
NaN payloads are neither preserved nor normalized for item names because NaN
float values are rejected rather than formatted.

Item paths used by the runtime and itemstore APIs have at most eight non-root
layers. Each layer is 1 to 32 bytes and may contain only ASCII letters, digits,
and `_`; the complete non-root path is at most 263 bytes including dots. These
checks apply before mutation as well as to lookup and deletion. When an API is
given a non-root item as its starting pointer, that item's ancestor depth and
path are included in the same limits. Invalid paths are rejected without
partially creating a path. The v1 root-name exception remains: the root name
is limited to 32 bytes but is not restricted to the non-root character set.

To assign an item, use the assignment operator, `=`.  If the item does not exist, it will be created (as will all of its parents, if it is a multi-layered item).  If the item exists, its value will be overwritten with the new value.  An item which does not exist has the default value of `nil`.  Thus:  
`foo = 10;`  
`bar = 10 * foo;`  
(bar is now equal to 100)  
`bar = 10 * wibble`  
(wibble does not exist, and has the default value of `nil`.  `10 * nil` is `nil`, so the value of bar is also `nil`)

The examples above are examples of immediate execution.  Once executed, the result is given and the steps to create it are forgotten.  However, let’s instead make bar a code item.  Code items are evaluated each time they are called.

`bar = code ( return 10 * wibble; );`
Now, bar is equal to `nil`, but if we define  
`wibble = 7;`  
then bar will be equal to 70.  If we redefine  
`wibble = 3;`  
then bar will now return 30.

Code items can contain local variables.  There is only one scope: the item.  Thus, a local variable is visible from the moment it is defined to the end of the item.  Local variables are defined by assignment.

`dingdong = code ( @a = bar; @b = 100; return @a + @b; );`

When `dingdong` is executed, it assigns the value of `bar` to local variable `@a`, the value of `100` to local variable `@b`, adds `@a` and `@b` together, and returns the result. `return expression;` evaluates its expression once and immediately exits the current code item; `return;` exits with `nil`. Every expression statement is evaluated and discarded, including the final one. Falling off the end of a code item returns `nil`.

When compiling code, if the parser doesn't like the source which it is chewing on, it will bail out and set `error` to an error number, and `error.msg` to the appropriate error message.  Thus an easy way to check if the code has compiled is to test these items.  A successful compilation will set these items to `nil`.

You can pass parameters to items, too.  If you pass arguments to an item which does not accept them, they are silently forgotten.  If you pass too many arguments, the extra ones are ignored.  If you pass too few, the missing ones have the value of `nil`.  Here is an item which takes two arguments:  
```
add = code {@a, @b} ( return @a + @b; );
if error then
  sys.log{"Compilation failed:\n"};
  sys.log{error.msg};
  sys.log{"\n"};
endif;
```

If you call `add` with no arguments, you are effectively calling `add{nil, nil};`, and because `+` treats `nil` as integer `0`, the result is integer `0`.  Calling `add{1};` is effectively `add{1, nil};` and returns `1`.  Calling `add{1, 2, 3};` returns `3`, because the third argument is intentionally dropped. This default behavior is a live-update design choice: callers can keep running while a code item's parameter list is being changed, whether a new parameter is added or an old parameter is removed. The same tolerant behavior applies when arguments are supplied to a missing item or to an expression that does not resolve to an item name. Starting `sin` with `--strict-runtime-contracts` keeps executing with the same stack/result behavior, but performs extra checks and records `ERR_RUNTIME_INVALIDARGS` in `error` plus a diagnostic in `error.msg` whenever `F`/item-call evaluation has to discard arguments.

Strict dropped-argument diagnostics are therefore opt-in. For example, `add{1, 2, 3};` still returns `3` in both modes, but strict mode also reports that one extra argument was discarded. Likewise, `missing.item{1};` still returns `nil`, but strict mode reports that the argument to the missing target was dropped. Run without `--strict-runtime-contracts` for normal live-update operation and lower runtime overhead; run with it when you want these mismatches surfaced during testing or development.

## Comments ##

Comments begin with `/*` and end with `*/`, and may include anything, including spaces.  Comments are disallowed from after the `code` keyword to before the opening `(` of the code definition, but all sensible uses of comments are allowed.

## Control structures ##

`IF condition THEN statements; ENDIF;`  
`IF condition THEN statements; ELSE statements; ENDIF;`  
`IF condition THEN statements; ELSIF condition THEN statements; ENDIF;`  
`IF condition THEN statements; ELSIF condition THEN statements; ELSE statements; ENDIF;`

`WHILE condition DO statements; ENDWHILE;`
`DO statements; WHILE condition;`

`DO` loops execute their body at least once, then repeat while the condition is
truthy. For example, `@a = 0; DO sys.log{@a}; @a++; WHILE @a < 5;` logs a
counter from zero through four. The loop itself has no result value.

`BREAK;` exits the nearest enclosing loop. `CONTINUE;` skips to the next
iteration of the nearest loop: in a `WHILE ... DO` loop it re-tests the
condition, while in a `DO ... WHILE` loop it proceeds to the trailing condition
before deciding whether to repeat. These statements cannot be used outside a
loop.

`RETURN;` can be used at any point to halt execution of the item and return
`nil`. `RETURN expression;` evaluates the expression once, halts immediately,
and returns that value. Falling off the end also returns `nil`; expression
statements never implicitly become an item's result.

## Values and operators ##

### Numeric values ###

Integer literals are base-10 whole numbers. Float literals are base-10 decimal literals with digits on both sides of the decimal point, optionally followed by an exponent, for example `0.5`, `1.0`, `-0.0`, `1.25e2`, or `6.02E+23`. A token such as `42` remains an integer literal, while malformed decimal forms such as `1.` are rejected. Special spellings such as `nan`, `inf`, and `infinity` are not accepted as source literals, although arithmetic can still produce IEEE 754 infinities and NaNs at runtime.

Sinistra floats are C `double` values and require IEEE 754 binary64 behavior: 53 bits of precision, binary radix, and the usual exponent range. Decimal float literals are converted with the C locale (`.` as the decimal separator) to the nearest binary64 value, including subnormal values, signed zero, and overflow to infinity when appropriate. Runtime arithmetic on floats follows the platform binary64 operations for addition, subtraction, multiplication, division, and unary negation.

When an arithmetic or numeric comparison operation sees one integer and one float, the integer operand is promoted to binary64 and the result is a float for arithmetic, or a boolean for comparison. Integer-only arithmetic is checked before each operation; signed 64-bit overflow in `+`, `-`, `*`, `/`, or unary negation produces `nil` rather than wrapping, saturating, or trapping. Integer remainder `%` truncates toward zero and any nonzero result has the sign of the left operand. Integer division by zero produces integer `0`, while integer `%` by zero produces `nil`; float division uses IEEE 754 behavior such as infinities or NaN, and float `%` uses `fmod()` (including NaN for a floating zero divisor). Invalid non-numeric operands produce `nil` for arithmetic remainder. String concatenation with `+` is still string-only. The `+` operator also treats `nil` as integer `0`, so `nil + 7` is `7`; other arithmetic operators do not treat `nil` as numeric.

Float equality and ordering use IEEE 754 comparison rules after any integer-to-float promotion. NaN is not equal to any value, including itself, so `NaN == NaN` is false and `NaN != NaN` is true when a NaN value is produced at runtime. Ordered comparisons involving NaN (`<`, `<=`, `>`, `>=`) are false. Positive and negative zero compare equal to each other and to integer `0`, but formatting preserves the sign: `+0.0` formats as `0.0` and `-0.0` formats as `-0.0`.

Truthiness treats `+0.0` and `-0.0` as false. Every other float value is true, including infinities and NaN.

Integer edge examples: `9223372036854775807 + 1`, `-9223372036854775808 - 1`, `3037000500 * 3037000500`, `-9223372036854775808 / -1`, and unary negation of `-9223372036854775808` all produce `nil` because they overflow signed 64-bit integer arithmetic. `5 / 0` produces integer `0`; `5 % 0` produces `nil`; `INT64_MIN % -1` produces integer `0`. `5.0 / 0.0` follows IEEE 754 and produces positive infinity, while floating `%` uses `fmod()` and `5.0 % 0.0` produces NaN.

### Operators ###

Arithmetic: `+`, `-`, `*`, `/`, `%` (same precedence and left associativity as `*` and `/`; integer arithmetic when both operands are integers; binary64 float arithmetic when either operand is a float).  The unary postfix operators `++` and `--` operate on local variables but not items, but note that these are statements, not expressions.  Thus the following is invalid:

`WHILE @a++ < 100 DO ...; ENDWHILE;`

The usual boolean comparison operators are present, and work in the same way as C.  The `||` and `&&` operators are not present: instead, use `or` and `and`.

Boolean literals are `true` and `false`; `nil` is the explicit nil literal (all
three are case-insensitive reserved words).

True values are: `true`, true outcomes of boolean operations, integer values which are not `0`, float values other than `+0.0` and `-0.0` (including NaN), and non-empty strings.  False values are: `false`, `nil`, integer `0`, float `+0.0` or `-0.0`, and the empty string (`""`). Nil compares equal to nil and remains distinct from `false`, `0`, and `""`.

Examples:
`is_wizard = true;`
`is_guest = false;`
`if is_wizard == true then ...; endif;`
`if is_guest == false then ...; endif;`

Strings may be concatenated with `+` but do not respond to other attempts to arithmetise them.

The usual operator precedence applies, and (parentheses) can be used to change this.

The `sys.exists{name}`, `sys.delete{name}`, `sys.nthname{name, index}`, and
`sys.rootname{index}` libcalls provide item-management helpers. Item names are
strings; relative names beginning with `.` are resolved against the current code
item where supported. `sys.exists` returns a boolean, `sys.delete` returns
`nil`, and the name lookups return a child name string or `nil`.  **Note:** item
order is not guaranteed.  Just because `foo` is the sixth child of `wibble` this
time, do not presume that it will be the sixth child the next time you start the
runtime engine.

## Tasks ##

An important concept to remember when writing Sinistra code is *no perpetual loops, ever*. The engine is built around a run-loop, which responds to network events, input callbacks, and timer tasks. The input timer has a nominal 10ms interval, but eligibility is not a real-time guarantee: other callbacks or a long-running input item can delay it. Each `net.input` call handles at most one pending connection, disconnection, or complete-line event, so queued events are handled over later fair-queue turns.

`task.newgametask{item, start, repeat}` uses integer intervals in tenths of a
second, converted to timer milliseconds. `start` is the delay before the first
callback and `repeat` is the interval between later callbacks. A zero repeat
interval makes the task one-shot; it retires automatically after its callback,
including the `start = 0, repeat = 0` immediate case. A positive repeat
interval keeps the task active until `task.killtask{id}` closes it. Intervals
must be non-negative integers no greater than `INT64_MAX / 100`; a task also
requires an initialized event loop and an existing item, and logs an error rather
than executing when that item is not a code item.
Within a timer task callback, `task.thisid` reports that task's id, including
while synchronously called items execute and after the task asks to close itself.
Outside such a callback it returns `nil`. `task.exists{id}` checks whether an id
is currently scheduled, and `task.count` reports how many scheduled tasks remain;
a close request makes both observations change immediately.

## Libraries ##

Libraries look like items, but they aren't, and they are read-only.  Don't try to assign something to a library function: it will not end well.  Library calls always return a value - this can be assumed to be `nil` unless otherwise stated.

The `sys` library does the sort of system-wide things that you might expect:  
`sys.backup` synchronously creates a timestamped backup of the itemstore as it
is currently held in memory and returns a boolean success result. The filename
uses the readable `YYYYMMDD-HHMMSS` suffix; when that target already exists,
`_1`, `_2`, and so on are chosen deterministically so existing backups are never
replaced. This return
type is an intentional compatibility change from the previous `nil` result.
`sys.save` synchronously checkpoints the current in-memory itemstore to the
configured primary itemstore path and returns a boolean success result. Runtime
state can continue changing after the call; those later mutations are not part
of the completed checkpoint. Both persistence calls pause event-loop progress
while writing. `false` means the configured durability level was not fully
confirmed and sets `ERR_RUNTIME_PERSISTENCE`; if failure occurs after atomic
publication or replacement, the target may nevertheless contain the new
snapshot. Successful persistence does not clear an unrelated existing `error`.
`sys.log{<expression>}` writes something to the system log: it takes an expression and will try to evaluate the expression and write something sensible in the log.  Do not abuse it.  
`sys.shutdown` will perform an orderly shutdown of the engine, saving the itemstore.  It takes no arguments.  
`sys.abort` will abort the engine without saving the itemstore.  It takes no arguments.
`sys.compile{<source>}` compiles and runs a string of Sinistra source code,
returns `true` on successful compilation/execution, and returns `false` for
invalid source input, compilation/setup failure, an unhandled runtime error, or
interrupted execution. A compiled program can handle a runtime error by setting
`error` back to `nil`; successful completion normalizes all `error` fields to
`nil`. The temporary code item's result is discarded, but its item mutations
remain in memory; the normal itemstore save at safe shutdown is what makes
those mutations durable.
`sys.exists{<name>}` reports whether a string-named item exists.
`sys.delete{<name>}` deletes a string-named item when it exists.
`sys.thisitem` returns the fully qualified name of the currently executing code
item. `sys.parentitem` returns that item's fully qualified namespace parent, or
`nil` for a top-level item; this is the namespace parent, not the call-stack
caller. `sys.calleritem` reports the immediate synchronous Sinistra caller, or
`nil` for a direct boot, input, or task entry. A temporary item run by
`sys.compile` treats the item that invoked `sys.compile` as its caller; an
ordinary item invoked by that temporary item sees the temporary item as its
caller. `sys.itemtype{<name>}` reports `"code"`, `"nil"`, `"bool"`, `"int"`,
`"float"`, or `"string"`, `sys.paramcount{<name>}` reports a readable code
item's declared parameter count, `sys.source{<name>}` returns that code item's
exact saved `source.sin` text from the configured source root, and
`sys.childcount{<name>}` reports the number of immediate children. Name
arguments use the normal item-name rules, including relative names resolved
from the executing item. Missing or invalid names return `nil`; non-string
names set `ERR_RUNTIME_INVALIDARGS`. When a code item exists but its source is
unavailable or cannot be represented as a Sinistra string, `sys.source`
returns an empty string where possible and sets `ERR_RUNTIME_SOURCE`.
`sys.nthname{<name>, <index>}` and `sys.rootname{<index>}` return child or root
item names by index. `sys.rootcount` and `sys.childcount` bound those
enumerations, but child ordering is not stable across runtime restarts.
`sys.version` returns a copy of the raw engine version string; [`SINVERSION`](../src/version.h)
is the source of truth. `sys.now` returns signed Unix-epoch milliseconds. `sys.monotime`
returns signed monotonic milliseconds from an unspecified operating-system
origin; only differences are meaningful, and readings do not decrease within
a process run. These successful introspection and time calls do not clear an
unrelated existing `error`.

The `net` library handles network activity:  
`net.input` checks to see if there is any interesting network activity.  It takes no arguments, processes at most one pending event per call, and advances a fair-queue cursor across zero-based connection slots.  A new connection returns `1`, a disconnection returns `2`, and data returns `3`.  If there is no activity, `0` is returned.  For connection, disconnection, and data events, `input.line` is set to the zero-based line number.  For data, `input.text` is set to the complete line of text without its terminating newline.  Data is only signalled after receiving a `\n` character from a connection, so the developer can be assured that if a line signals that data has been received, they will be processing a whole line of input. A disconnection event destroys the line and makes the slot reusable before `net.input` returns.
`net.maxlines` returns the configured maximum connection-slot count. It bounds enumeration of the zero-based slots, so valid indexes range from `0` through `net.maxlines - 1`; it does not change an unrelated existing `error`.
`net.connected{<integer>}` is the safe predicate for checking each slot while enumerating that range. It returns `true` only when the slot has a Telnet session and is in the writable `LINE_idle` or `LINE_data` state; it returns `false` for connecting, disconnecting, empty, missing-Telnet, or otherwise unavailable slots. A non-integer, negative, or out-of-range line returns `nil` and sets `ERR_RUNTIME_INVALIDARGS`. Valid true/false results do not change an unrelated existing `error`.
`net.address{<integer>}` returns the numeric IPv4 or IPv6 peer address as a string only for a writable Telnet line with a non-empty address. It returns `nil` without changing `error` for connecting, disconnecting, empty, missing-Telnet, or empty-address slots. A non-integer, negative, or out-of-range line returns `nil` and sets `ERR_RUNTIME_INVALIDARGS`. A disconnection event from `net.input` destroys the line, so game code must retain the returned string before that event if it needs the address afterward.
`net.write{<integer>, <expr>}` writes text to a line.  It takes two arguments: if the first argument is not an integer connection index in range, the libcall sets `ERR_RUNTIME_INVALIDARGS` and returns `nil`; if it is in range but no longer writable, the libcall returns `nil` without changing `error`.  Output limit or backpressure rejection returns `false`.  Otherwise, the second expression is evaluated and sent to the connection.  As with `sys.log` the engine will try to convert this to a string if it is a value of another type, and will do its best to do the right thing.
`net.echo{<expr>}` changes Telnet echo negotiation for the line selected by the most recent `net.input` fair-queue turn. It always returns `nil`. A truthy expression sends `WONT ECHO`, which deliberately leaves echoing with the local client; a falsy expression sends `WILL ECHO`, which claims server echo and deliberately turns local/client echo off. Sinistra only negotiates this protocol option and never echoes received input itself. If the cursor is out of range or that line has no active Telnet session, the call is a no-op without changing `error`.
`net.flush{<integer>}` requests an immediate flush of pending output for an active line.  It returns `true` when the line is active, `false` when the line is out of range or inactive, and `nil` when the argument is not a non-negative integer.  Invalid arguments set `ERR_RUNTIME_INVALIDARGS`; out-of-range or inactive-line failures set the network error item.
`net.ditch{<integer>}` marks an active line for graceful disconnection.  It returns `true` when the line is active and has been marked, `false` when the line is out of range or already inactive/disconnecting, and `nil` when the argument is not a non-negative integer.  Invalid arguments set `ERR_RUNTIME_INVALIDARGS`; out-of-range or inactive-line failures set the network error item. Pending output is flushed first, and the handle closes after queued or in-flight output drains; the later `net.input` disconnection event destroys the line and makes the slot reusable.

The `task` library is described in the task lifecycle above. `task.newgametask`
returns an integer task id on success; a zero repeat interval makes the task
one-shot, while a positive repeat interval keeps it active until killed.
`task.killtask{<integer>}` takes one argument, which evaluates to the id of the task to be killed.  If the argument is not an integer, the libcall sets `ERR_RUNTIME_INVALIDARGS` and returns `nil`.  If the task does not exist or the id is negative, the libcall returns `false` without changing `error`.  Otherwise, the task is removed from the list of scheduled tasks and the libcall returns `true`.
`task.exists{<integer>}` returns whether that task is currently scheduled, while
`task.count` returns the number of scheduled tasks. `task.thisid` returns the
currently executing timer task id or `nil` in ordinary runtime contexts.

The `str` library contains libcalls which operate on or produce string values:  
`str.capitalise{<expr>}` capitalises the first letter of the given string.  
`str.lower{<expr>}` converts the whole string to lowercase.  
`str.upper{<expr>}` converts the whole string to uppercase.  
`str.len{<expr>}` returns the byte length of a string.  
`str.trim{<expr>}`, `str.ltrim{<expr>}`, and `str.rtrim{<expr>}` trim leading and/or trailing whitespace.  
`str.substr{<text>, <start>, <len>}` returns a byte substring.  
`str.find{<text>, <needle>}` returns the first byte offset of `needle`, or `-1` if it is not present.  
`str.contains{<text>, <needle>}` returns whether `needle` occurs in `text`.  
`str.startswith{<text>, <prefix>}` and `str.endswith{<text>, <suffix>}` test string prefixes and suffixes.  
`str.eqcasei{<left>, <right>}` compares strings with ASCII case folding.  
`str.valtostr{<expr>}` converts a value to a string, passing strings through unchanged.  
`str.replace{<text>, <old>, <new>}` replaces all occurrences of `old` in `text` with `new`.  
`str.repeat{<text>, <count>}` repeats `text` `count` times.  
`str.padleft{<text>, <width>}` and `str.padright{<text>, <width>}` pad text with spaces up to `width`.  


## Opcode schema workflow

IR opcode semantics are centralized in `src/compiler/ir/opcode_schema.def`.
When adding a new opcode, update exactly one schema row (`OP(...)`) with:
- enum name (must match `IR_OP_<NAME>`)
- encoded symbol
- operand kind
- size policy
- validator policy

`src/compiler/ir.c` materializes the schema into `g_ir_opcode_schema` and `src/compiler/emitbc.c` consumes it for encoding, size accounting, and validator dispatch. Add any truly custom payload writing logic in `src/compiler/emitbc.c` only when the schema policy requires variable-length handling.

Run `make test` to validate schema consistency checks and emitter behavior.
