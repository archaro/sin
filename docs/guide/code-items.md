# Code Items

A code item can be thought of as conceptually similar to the FORTH "word: it is a piece of bytecode which is executed when called.  It may take arguments, and it may return a value - but it need do neither of these things.

On the outside, a code item looks exactly like any other item and can be treated in the same way.  Consider this:

`foo = code ( sys.log{"Foo!\n"}; );`

`bar = 42;`

The item called `bar` is, of course, just a simple item which contains the integer value 42, whereas `foo` contains code which, when executed, writes a fixed message to the system log, and does not hold a value.  So if we execute `baz = foo + bar;` what do we expect will be the value of `baz`?  Whatever we expect, the answer - as is so often the case - is 42.  This is because if a code item which doesn't return a value is used in an expression, it is deemed to return `nil`.  And 42 plus nil is equal to 42.  But say we rewrite `foo` like this:

`foo = code ( sys.log{"Foo!\n"}; return 5; );`

Now something interesting happens when we evaluate `foo + baz`: it still writes to the system log but additionally returns the integer value 5, giving us a total of 47.  Isn't that lovely?

Code items can be practically any size, and can be as complex as you want. They can call other items, they can contain control structures such as IF, WHILE, and FOREACH, and they can make libcalls (calls to Sinistra libraries).  This flexibility gives Sinistra its power.

## Local Variables

Local variables are prefixed with @, and are visible from the point of definition to the end of the code, whereupon they cease to exist.  Trying to use a local variable before it is defined will result in a compiler error.  Thus this is allowed: `@a = 5; sys.log{@a};`, but this will not compile: `@a = 5; sys.log{@a * @b};`

A code item can have up to 255 local variables but be aware that a FOREACH loop secretly uses three of them.
