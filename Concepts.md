# How to be Sinister #

## The Item ##

The fundamental unit in Sinistra is the *item*.  An item can contain many things: integers, strings, Boolean values or `nil`, or it can contain code.  A value item simply returns its value, whereas a code item executes its code and returns the result.  All items return a value (even if the value is `nil`).  Items can also call other items.

Items are nominally hierarchical, although this is only an organisational strategy – there is no inheritance.  Thus the following are items:  
`foo`  
`foo.bar`  
`foo.bar.baz`  
…although there is nothing can be inferred from `foo.bar` by its relationship with `foo`.

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

You can pass parameters to items, too.  If you pass arguments to an item which does not accept them, they are silently forgotten.  If you pass too many arguments, the extra ones are ignored.  If you pass too few, the missing ones have the value of `nil`.  Here is an item which takes two arguments:  
`add = code {@a, @b} ( @a + @b; );`

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

## Operators ##

Arithmetic: `+`, `-`, `*`, `/` (all integer arithmetic).  The unary postfix operators `++` and `--` operate on local variables but not items, but note that these are statements, not expressions.  Thus the following is invalid:

`WHILE @a++ < 100 DO ...; ENDWHILE;`

The usual boolean comparison operators are present, and work in the same way as C.  The `||` and `&&` operators are not present: instead, use `or` and `and`.

True values are: true outcomes of boolean operations, integer values which are not `0`, and empty strings (`""`).  Everything else is false.

Strings may be concatenated with `+` but do not respond to other attempts to arithmetise them.

The usual operator precedence applies, and (parentheses) can be used to change this.

There are some unary operators which look like items, but are not:  
`exists{<expr>}` evaluates the expression and checks it if is an item, and if it exists.  
`delete{<expr>` evaluates the expression and checks if it an item, then deletes it, always returning `nil`.  
`nthname{<expr>, <expr>}` evaluates the first expression as an item and, if it exists, evaluates the second item as zero-based index, and returns the name of the child at that index.  If the item does not exist or the index is out of range, `nil` is returned.  This makes it possible to loop over all the children of a given item.  **Note:** item order is not guaranteed.  Just because `foo` is the sixth child of `wibble` this time, do not presume that it will be the sixth child the next time you start the runtime engine.  
`rootname{<expr>}` is exactly the same as `nthname` with the exception that it operates at the root of the item tree, and takes only an index.

## Tasks ##

An important concept to remember when writing Sinistra code is *no perpetual loops, ever*.  The engine is built around a run-loop, which responds to certain events.  The most important events are network events - connections, disconnections, and data - and there is limited control of these in-game.  Another important event is the *input* event, which is called approximately every 100ms by the run-loop, and which checks to see if there is any outstanding network activity to process.  The *input* event executes the `input` item, which is, technically, the only code item which *needs* to be created in order to have a functional system.  This item will need to call the net.input` library call (also known as a libcall) and should then react appropriately to the network input received.  The last sort of event is the *task*: tasks are Sinistra code items which are executed according to a timer schedule - either once at a predetermined point, or repeted at a set interval.  Task management is entirely controlled within Sinistra, and (within reason) can do anything that the developer desires.  Tasks are either central or per-line, which means that they can be allocated to individual players.  A typical example of this would be to create a task that times-out the player after a period of idleness.  Because the creation and management of such a task is entirely within the management of Sinistra code, each individual time-out timer can be configured according to who is connected to the line: 15 seconds for a new login before the player character is loaded, 1 minute for a newbie, 30 minutes for a wizard, etc.

## Libraries ##

Libraries look like items, but they aren't, and they are read-only.  Don't try to assign something to a library function: it will not end well.  Library calls always return a value - this can be assumed to be `nil` unless otherwise stated.

The `sys` library does the sort of system-wide things that you might expect:  
`sys.backup` creates a backup of the itemstore as it is currently held in memory.  
`sys.log{<expression>}` writes something to the system log: it takes and expression and will try to evaluate the expression and write something sensible in the log.  Do not abuse it.  
`sys.shutdown` will perform an orderly shutdown of the engine, saving the itemstore.  It takes no arguments.  
`sys.abort` will abort the engine without saving the itemstore.  It takes no arguments.

The `net` library creates and manages tasks:  
`sys.input` checks to see if there is any interesting network activity.  It takes no arguments but returns a value and *may* set an item, depending on what activity it is reporting.  A new connection returns `1`, a disconnection returns `2`, and data returns `3`.  If there is no activity, `0` is returned.  If there is data, subitems of the `input` item will be set: `input.line` will be set to the line number that sent the data, and `input.text` will be set to the data that has been received.  Data is only signalled after receiving a `/n` character from a connection, so the developer can be assured that if a line signals that data has been received, they will be processing a whole line of input.  
`sys.write{<integer>, <expr>}` writes text to a line.  It takes two arguments: if the first argument does not evaluate to a currently-connected line, this libcall fails silently - the developer does not need to worry about writing to a connection which is no longer there.  Otherwise, the second expression is evaluated and sent to the connection.  As with `sys.log` the engine will try to convert this to a string if it is a value of another type, and will do its best to do the right thing.

The `task` library is for anything relating to network activity:  
`task.newgametask{<expr>, <integer>, <integer>}` evaluates the first argument and, if it comes out as an existing code item, evaluate the second and third arguments.  The second argument, if it evaluates to an integer greater than 0, is the number of centiseconds after which the item in the first argument will be executed.  The third argument, if it evaluates to an integer greater than 0, is the interval (expressed in centiseconds) between executions of the item.  If both the second and third arguments evaluate to 0, the item will not be executed, and no task will be created.  If the interval is greater than 0, the task will repeat endlessly until killed.  Returns an integer, which is the task id.  
`task.killtask{<integer>}` takes one argument, which evaluates to the id of the task to be killed.  If the task does not exist, the libcall fails silently.  Otherwise, the task is removed from the list of scheduled tasks.

The `str` library contains libcalls which operate on string values.  They have no effect on non-string values:  
`str.capitalise{<expr>}` capitalises the first letter of the given string.  
`str.lower{<expr>}` converts the whole string to lowercase.  
`str.upper{<expr>}` converts the whole string to uppercase.  

