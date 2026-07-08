# How to be Sinister #

The style of the language is something like the misbegotten offspring of Forth and Smalltalk.

## Flow of Operations ##

When the runtime engine starts up, it first loads and executes the bootstrap code (which is separately compiled).  The engine is event-driven and this code sets things up ready for the game to run, including setting up the main game tasks.  Tasks are attached to the runloop and are called as necessary.  There are three kinds:
- Network tasks: the listener, and any player connections created by it.  These tasks run outside the game and interact in limited ways with *Sinistra* code, and their purpose is to manage input from and output to connected players.
- Timer tasks: these are managed by *Sinistra* code (for example, the bootstrap code).  Each time the timer expires, the specified code is run.
- Input task: this is the most important task, and is run once per loop.  It processes connections, disconnections and data from the players and output back to them.  The input task expects to call the `input` item, which is written in *Sinistra*.  (And is, in fact, the only code item you *need* to write, making sure it calls the net.input libcall.)

## The Item ##

The fundamental unit in Sinistra is the *item*.  An item can contain many things: integers, floats, strings, Boolean values or `nil`, or it can contain code.  A value item simply returns its value, whereas a code item executes its code and returns the result.  All items return a value (even if the value is `nil`).  Items can also call other items.

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

To assign an item, use the assignment operator, `=`.  If the item does not exist, it will be created (as will all of its parents, if it is a multi-layered item).  If the item exists, its value will be overwritten with the new value.  An item which does not exist has the default value of `nil`.  Thus:  
`foo = 10;`  
`bar = 10 * foo;`  
(bar is now equal to 100)  
`bar = 10 * wibble`  
(wibble does not exist, and has the default value of `nil`.  `10 * nil` is `nil`, so the value of bar is also `nil`)

The examples above are examples of immediate execution.  Once executed, the result is given and the steps to create it are forgotten.  However, let’s instead make bar a code item.  Code items are evaluated each time they are called.

`bar = code ( 10 * wibble; );`  
Now, bar is equal to `nil`, but if we define  
`wibble = 7;`  
then bar will be equal to 70.  If we redefine  
`wibble = 3;`  
then bar will now return 30.

Code items can contain local variables.  There is only one scope: the item.  Thus, a local variable is visible from the moment it is defined to the end of the item.  Local variables are defined by assignment.

`dingdong = code ( @a = bar; @b = 100; @a + @b; );`

When `dingdong` is executed, it assigns the value of `bar` to local variable `@a`, the value of `100` to local variable `@b`, adds `@a` and `@b` together, and returns the result.  Note that there is no `return` statement required.  The value of the last statement in the code is the value of the item (in this case `@a + @b`).

When compiling code, if the parser doesn't like the source which it is chewing on, it will bail out and set `error` to an error number, and `error.msg` to the appropriate error message.  Thus an easy way to check if the code has compiled is to test these items.  A successful compilation will set these items to `nil`.

You can pass parameters to items, too.  If you pass arguments to an item which does not accept them, they are silently forgotten.  If you pass too many arguments, the extra ones are ignored.  If you pass too few, the missing ones have the value of `nil`.  Here is an item which takes two arguments:  
```
add = code {@a, @b} ( @a + @b; );
if error then
  sys.log{"Compilation failed:\n"};
  sys.log{error.msg};
  sys.log{"\n"};
endif;
```

If you call `add` with no arguments, you are effectively calling `add{nil, nil};`, and so the result is `nil`.  Calling `add` with only one parameter also returns `nil` because if you add `nil` to anything, the result is always nil.  Calling `add{1, 2, 3};` returns 3, because the third argument is silently dropped.

## Comments ##

Comments begin with `/*` and end with `*/`, and may include anything, including spaces.  Comments are disallowed from after the `code` keyword to before the opening `(` of the code definition, but all sensible uses of comments are allowed.

## Control structures ##

`IF condition THEN statements; ENDIF;`  
`IF condition THEN statements; ELSE statements; ENDIF;`  
`IF condition THEN statements; ELSIF condition THEN statements; ENDIF;`  
`IF condition THEN statements; ELSIF condition THEN statements; ELSE statements; ENDIF;`

`WHILE condition DO statements; ENDWHILE;`

`RETURN` can be used at any point to halt execution of the item.  It takes no parameter; when execution ends, the value of the item is whatever is on the top of its stack.

## Values and operators ##

### Numeric values ###

Integer literals are base-10 whole numbers. Float literals are base-10 decimal literals with digits on both sides of the decimal point, optionally followed by an exponent, for example `0.5`, `1.0`, `-0.0`, `1.25e2`, or `6.02E+23`. A token such as `42` remains an integer literal, while malformed decimal forms such as `1.` are rejected. Special spellings such as `nan`, `inf`, and `infinity` are not accepted as source literals, although arithmetic can still produce IEEE 754 infinities and NaNs at runtime.

Sinistra floats are C `double` values and require IEEE 754 binary64 behavior: 53 bits of precision, binary radix, and the usual exponent range. Decimal float literals are converted with the C locale (`.` as the decimal separator) to the nearest binary64 value, including subnormal values, signed zero, and overflow to infinity when appropriate. Runtime arithmetic on floats follows the platform binary64 operations for addition, subtraction, multiplication, division, and unary negation.

When an arithmetic or numeric comparison operation sees one integer and one float, the integer operand is promoted to binary64 and the result is a float for arithmetic, or a boolean for comparison. Integer-only arithmetic is checked before each operation; signed 64-bit overflow in `+`, `-`, `*`, `/`, or unary negation produces `nil` rather than wrapping, saturating, or trapping. This includes `INT64_MIN / -1` and unary negation of `INT64_MIN`. Integer division by zero keeps the historical behavior and produces integer `0`, while float division uses IEEE 754 behavior such as infinities or NaN. String concatenation with `+` is still string-only, and invalid mixed non-numeric types produce `nil` or `false` according to the operator.

Float equality and ordering use IEEE 754 comparison rules after any integer-to-float promotion. NaN is not equal to any value, including itself, so `NaN == NaN` is false and `NaN != NaN` is true when a NaN value is produced at runtime. Ordered comparisons involving NaN (`<`, `<=`, `>`, `>=`) are false. Positive and negative zero compare equal to each other and to integer `0`, but formatting preserves the sign: `+0.0` formats as `0.0` and `-0.0` formats as `-0.0`.

Truthiness treats `+0.0` and `-0.0` as false. Every other float value is true, including infinities and NaN.

### Operators ###

Arithmetic: `+`, `-`, `*`, `/` (integer arithmetic when both operands are integers; binary64 float arithmetic when either operand is a float).  The unary postfix operators `++` and `--` operate on local variables but not items, but note that these are statements, not expressions.  Thus the following is invalid:

`WHILE @a++ < 100 DO ...; ENDWHILE;`

The usual boolean comparison operators are present, and work in the same way as C.  The `||` and `&&` operators are not present: instead, use `or` and `and`.

Boolean literals are `true` and `false`.

True values are: `true`, true outcomes of boolean operations, integer values which are not `0`, float values other than `+0.0` and `-0.0` (including NaN), and non-empty strings.  False values are: `false`, `nil`, integer `0`, float `+0.0` or `-0.0`, and the empty string (`""`).

Examples:
`is_wizard = true;`
`is_guest = false;`
`if is_wizard == true then ...; endif;`
`if is_guest == false then ...; endif;`

Strings may be concatenated with `+` but do not respond to other attempts to arithmetise them.

The usual operator precedence applies, and (parentheses) can be used to change this.

There are some unary operators which look like items, but are not:  `exists{<expr>}` evaluates the expression and checks it if is an item, and if it exists. Returns a boolean value. 
`delete{<expr>` evaluates the expression and checks if it an item, then deletes it. No value is returned.
`nthname{<expr>, <expr>}` evaluates the first expression as an item and, if it exists, evaluates the second item as zero-based index, and returns the name of the child at that index.  If the item does not exist or the index is out of range, `nil` is returned.  This makes it possible to loop over all the children of a given item.  **Note:** item order is not guaranteed.  Just because `foo` is the sixth child of `wibble` this time, do not presume that it will be the sixth child the next time you start the runtime engine.  
`rootname{<expr>}` is exactly the same as `nthname` with the exception that it operates at the root of the item tree, and takes only an index.

## Tasks ##

An important concept to remember when writing Sinistra code is *no perpetual loops, ever*.  The engine is built around a run-loop, which responds to certain events.  The most important events are network events - connections, disconnections, and data - and there is limited control of these in-game.  Another important event is the *input* event, which is called approximately every 100ms by the run-loop, and which checks to see if there is any outstanding network activity to process.  The *input* event executes the `input` item, which is, technically, the only code item which *needs* to be created in order to have a functional system.  This item will need to call the `net.input` library call (also known as a libcall) and should then react appropriately to the network input received.  The last sort of event is the *task*: tasks are Sinistra code items which are executed according to a timer schedule - either once at a predetermined point, or repeted at a set interval.  Task management is entirely controlled within Sinistra, and (within reason) can do anything that the developer desires.  Tasks are either central or per-line, which means that they can be allocated to individual players.  A typical example of this would be to create a task that times-out the player after a period of idleness.  Because the creation and management of such a task is entirely within the management of Sinistra code, each individual time-out timer can be configured according to who is connected to the line: 15 seconds for a new login before the player character is loaded, 1 minute for a newbie, 30 minutes for a wizard, etc.

## Libraries ##

Libraries look like items, but they aren't, and they are read-only.  Don't try to assign something to a library function: it will not end well.  Library calls always return a value - this can be assumed to be `nil` unless otherwise stated.

The `sys` library does the sort of system-wide things that you might expect:  
`sys.backup` creates a backup of the itemstore as it is currently held in memory.  
`sys.log{<expression>}` writes something to the system log: it takes and expression and will try to evaluate the expression and write something sensible in the log.  Do not abuse it.  
`sys.shutdown` will perform an orderly shutdown of the engine, saving the itemstore.  It takes no arguments.  
`sys.abort` will abort the engine without saving the itemstore.  It takes no arguments.

The `net` library handles network activity:  
`net.input` checks to see if there is any interesting network activity.  It takes no arguments but returns a value and *may* set an item, depending on what activity it is reporting.  A new connection returns `1`, a disconnection returns `2`, and data returns `3`.  If there is no activity, `0` is returned.  If there is data, subitems of the `input` item will be set: `input.line` will be set to the line number that sent the data, and `input.text` will be set to the data that has been received.  Data is only signalled after receiving a `/n` character from a connection, so the developer can be assured that if a line signals that data has been received, they will be processing a whole line of input.  
`net.write{<integer>, <expr>}` writes text to a line.  It takes two arguments: if the first argument does not evaluate to a currently-connected line, this libcall fails silently - the developer does not need to worry about writing to a connection which is no longer there.  Otherwise, the second expression is evaluated and sent to the connection.  As with `sys.log` the engine will try to convert this to a string if it is a value of another type, and will do its best to do the right thing.

The `task` library is for anything relating to network activity:  
`task.newgametask{<expr>, <integer>, <integer>}` evaluates the first argument and, if it comes out as an existing code item, evaluate the second and third arguments.  The second argument, if it evaluates to an integer greater than 0, is the number of centiseconds after which the item in the first argument will be executed.  The third argument, if it evaluates to an integer greater than 0, is the interval (expressed in centiseconds) between executions of the item.  Negative start or repeat intervals are invalid, as are intervals above `INT64_MAX / 100` because they cannot be safely converted to timer milliseconds.  If both the second and third arguments evaluate to 0, the item will not be executed, and no task will be created.  If the interval is greater than 0, the task will repeat endlessly until killed.  Returns an integer, which is the task id.
`task.killtask{<integer>}` takes one argument, which evaluates to the id of the task to be killed.  If the task does not exist, the libcall fails silently.  Otherwise, the task is removed from the list of scheduled tasks.

The `str` library contains libcalls which operate on string values.  They have no effect on non-string values:  
`str.capitalise{<expr>}` capitalises the first letter of the given string.  
`str.lower{<expr>}` converts the whole string to lowercase.  
`str.upper{<expr>}` converts the whole string to uppercase.  


## Opcode schema workflow

IR opcode semantics are centralized in `src/compiler/ir/opcode_schema.def`.
When adding a new opcode, update exactly one schema row (`OP(...)`) with:
- enum name (must match `IR_OP_<NAME>`)
- encoded symbol
- operand kind
- size policy
- validator policy

`ir.c` materializes the schema into `g_ir_opcode_schema` and `emitbc.c` consumes it for encoding, size accounting, and validator dispatch. Add any truly custom payload writing logic in `emitbc.c` only when the schema policy requires variable-length handling.

Run `make test` to validate schema consistency checks and emitter behavior.
