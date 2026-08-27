# How to be Sinister - Examples of Sinister Behaviour

## What This Document Is Not
First things first.  This document isn't a tutorial.  If you are looking for a
tutorial, you should probably read [Your First World](first-world.md).

If, however, you are looking for specific examples of how to do a thing, you
may find your answer here.  The first two sections go through the example
worlds (one of which you will already have tested if you walked through the
[Quick Start](quickstart.md) tutorial) included in the Sinistra distribution.
Following on from those are a series of snippets of example code, explaining
how a thing is so.  For the *why* of a thing, you probably need the
[Sinistra Reference](../reference/) or [Internals](../internals/) sections.

## The Echo Server
The Sinistra source files for this example are
[echo-load.src](examples/echo-load.src) and
[echo-boot.src](examples/echo-boot.src).

This world is probably the simplest Sinistra project which can be said to Do a
Thing.  You connect to it, you type something, it echoes back what you type.
The purpose of this example is to show how a network-aware application is
written.

NB: Sinistra's network interface understands the basic Telnet protocol.

```sinistra
input = code (
```
By default, the Sinistra runtime engine (`sin`) assumes that your input item
is called `input`.  If you really want to change it, you can pass the option
`-n <item name>` or `--input <item name>` when you run `sin`.

```sinistra
@a = net.input;
```
The `net.input` libcall is what makes Sinistra a network-aware language.  When
you start `sin` (unless you use `--loadonly`) it begins to listen on port 4001
(you can change that with `-p`/`--port`).  The runtime engine operates a
single-threaded event loop, which listens for new connections and activity on
existing connections. Network events are made available to Sinistra code
through `net.input`, which processes at most one pending event per call.
If it is a new connection, `input.line` is set to the connection number and
`net.input` returns 1.  If it is data from an existing line, `input.line` is
set to the connection number and `input.text` is set to the data which was
sent.  In this case, `net.input` returns 3.  A disconnection sets `input.line`
to the disconnected connection number, and `net.input` returns 2.  If there was
no activity, `net.input` returns 0.  Whatever the value of `net.input`, it is
stored in local variable `@a`.

```sinistra
if @a == 1 then
  net.write{input.line, "Hello!\n"};
elsif @a == 3 then
  net.write{input.line, input.text};
  net.write{input.line, "\n"};
endif;
```
This is a simple if-ladder, doing much the same work as a switch statement.
Note that we only care about new connections (so that we can send a greeting)
and data from existing connections (so that we can echo it right back).  The
value of `@a` can also be `0` or `2`, but we don't care about them so they are
not tested for.

```sinistra
);
```
And that's it.  The end of the `input` item.  There is no looping here,
because `input` is called repeatedly by the event loop.  In fact, an
unbreakable loop anywhere in Sinistra code will break the model and cause
everything to stop working.

The bootstrap code for the echo server is trivial.  It merely prints a message
to the log to say that the server is starting.  A bootstrap file is necessary
for the runtime engine to start.  It is a useful place to reset ephemeral
items and other one-time setup which needs to run before the world opens for
network connections - but it doesn't *have* to have anything in it.  It is
quite possible to compile a bootstrap bytecode file from an empty source file
if that's what sweetens your tea.

## The Chat Server
The Sinistra source files for this example are
[chat-load.src](examples/chat-load.src) and
[chat-boot.src](examples/chat-boot.src).

Although marginally more complex than the echo server, the chat server is still
extremely simple.  This example will go through the additional elements of the
code in `chat-load.src`, but will not go over material covered in the
discussion of the echo server.

```sinistra
@l = 0;
while @l < 8 do
  if @l != input.line then
    net.write{@l, "Line "};
    net.write{@l, input.line};
    net.write{@l, " has connected.\n"};
  endif;
  @l++;
endwhile;
```
This is a simple while-loop, which iterates until `@l >= 8.`  As with many
other programming languages, a careless loop which does not increment its
condition value will be the source of much unhappiness.  (NB: the choice of 8
connections is arbitrary for the example - `net.maxlines` will return the
number of connections that the runtime engine is currently configured to
handle).

```sinistra
if str.startswith{input.text, "\\"} then
  @cmd = str.substr{input.text, 1, str.len{input.text}};
  docommand{input.line, @cmd};
else
```
This is an interesting if-branch: it demonstrates three string libcalls.  In
the condition, the contents of `input.text` are examined to see if they begin
with the `\` character (note that it is escaped with another `\`).  If so, a
new string is constructed which consists of the remainder of `input.text` after
the initial `\`.  This is naive and there are better ways to do it, but it
illustrates string manipulation quite well.

Also in this section, the item `docommand` is called. If `docommand` were a
value item, no code would run: its stored value would simply be returned and,
because this is an expression statement, discarded. However, you will see later
in the example that `docommand` is a code item, so it executes synchronously
with `input.line` and `@cmd` as arguments.

```sinistra
docommand = code {@l, @cmd} (
```
Here we define `docommand` as a code item taking two arguments.  Sinistra is
deliberately forgiving about argument counts: if a code item expects more
arguments than are supplied, the missing arguments are initialised to `nil`. If
more arguments are supplied than the item declares, the excess arguments are
silently dropped by default (if, however `sin` is started with
`--strict-runtime-contracts` such mismatches will be reported in the log,
although the behaviour is not changed).

```sinistra
if str.eqcasei{@cmd, "quit"} then
```
Another string manipulation libcall which does a case-insensitive comparison.

```sinistra
net.write{@l, "You have been disconnected.\n"};
net.flush{@l};
net.ditch{@l};
```
Three network libcalls: write to a connection, request that all pending output
be flushed to the client, then request that the connection be closed. In this
example the explicit flush is important: without it, the final message may not
reach the client before the connection closes.

```sinistra
sys.shutdown;
```
A powerful system libcall, `sys.shutdown` closes all connections, terminates
the event loop, persists the item tree to disk, and causes the runtime engine
to exit normally.  If you wanted to terminate without persisting the item tree
to disk (also known as "termination with extreme prejudice"), use `sys.abort`.

## References and Dereferences
Consider this code:
```sinistra
wibble;
```
An item expression evaluates the item. If it is a value item, its stored value
is returned; if it is a code item, the code is executed synchronously and its
return value is used (or `nil` if no return value is given). Here `wibble;` is
an expression statement, so whatever value results is simply discarded.

But what happens when we do this?
```sinistra
wibble = code ( sys.log{"Wibble!\n"}; );
@deleted = sys.delete{wibble};
```
You could try compiling this and running it with `sin --loadonly`.  You will
see that rather than `wibble` being deleted, it is in fact executed!  But wait,
there's more: `wibble` returns `nil` (the default value if no return is given),
which means that `sys.delete` is passed a `nil` argument.  Since `nil` is
neither an item name nor an item reference, it returns `nil` and records an
"invalid arguments" error.  Were you to run the code, you would see something
like this:
```
Wibble!
Bootstrap execution left an error:
Invalid arguments to library call. (sys.delete item name must be a string or item reference)
```
...which is absolutely not what is required.

To delete the item we need to tell Sinistra that we mean "this item itself",
not "whatever this item happens to do".  For that, we require the reference
operator, `&`.  This code works exactly as expected:
```sinistra
wibble = code ( sys.log{"Wibble!\n"}; );
@deleted = sys.delete{&wibble};
```
Here `deleted` is `true` because the referenced item was removed.  `sys.delete`
returns `false` when a valid target is absent, or when deletion is refused for
an execution-pinned or protected target.
Some libcalls specifically require an item-reference, while others accept
either an item-reference or a string containing an item name. Check the
reference manual for the specifics of each call.

Contrariwise, in order for Sinistra to dynamically access items, there needs to
be a way of saying "use this value as a layer of the item path being
constructed".  This is where the dereference operator `[...]` comes in.
Consider the following:
```sinistra
foo.1 = "snap ";
foo.2 = "crackle ";
foo.3 = "pop\n";
@l = 1;
while @l <= 3 do
  sys.log{foo.[@l]};
  @l++;
endwhile;
```
This will output:
```
snap crackle pop
```
Conceptually, Sinistra is taking `foo.[@l]` and converting it into a
fully-qualified item for each value of `@l`: `foo.1`, `foo.2`, and `foo.3`.

You can use more than one dereference in an item path, and dereferences may
themselves contain item expressions. Each dereference must be evaluated at
runtime, so it involves a little more work than a literal layer. Usually that
is exactly what you want; the key thing to remember is to avoid gratuitously
complicated dereference chains in frequently executed code.

## Lists and `foreach`
Every good programming language needs lists, and Sinistra is no exception. A
list is a collection of values. Those values can be strings, integers, other
lists, item references, or any delightful mixture thereof. Consider the
following:
```sinistra
rooms.1.desc = "A small stone room.";
rooms.2.desc = "A surprisingly large cupboard.";
rooms.3.desc = "Somewhere damp.";

@tour = #[&rooms.1.desc, &rooms.2.desc, &rooms.3.desc];

foreach @room in @tour do
  sys.log{sys.fetch{@room}};
  sys.log{"\n"};
endfor;
```

We have created three items with string values, and then created a list of
the references to those items.  We then use `foreach` to iterate over the list
and `sys.fetch` to dereference each item-reference as a value (contrast this
with the syntactic dereference `[...]` we discussed earlier). This snippet
would print each string on a new line.

The `foreach` control structure evaluates the list expression once and then
visits each stored value in order; @room is an ordinary local variable and
retains the last value visited after the loop ends.

Note that we could have used the item expressions directly in the list constructor instead:
```sinistra
@tour = #[rooms.1.desc, rooms.2.desc, rooms.3.desc];
```
This would evaluate those items when the list was constructed and store their
string values, obviating the need to call sys.fetch during iteration — but
where would be the fun in that?

One important point to remember about lists: they are immutable.  Consider:
```sinistra
@a = #[1, 2, 3];
@b = list.append{@a, 4};
```
In this example, `@a` is still `#[1, 2, 3]`, while @b is #[1, 2, 3, 4]. List
operations return new lists rather than modifying their inputs.

## Dynamic Code Invocation
This is a fundamental Sinistra concept and there are two paradigms. These are
demonstrated below by way of creating a dispatcher item.

Let's say that in your world, commands all live under the `command` item and
take exactly one argument.  The easiest way to dispatch commands is to use
dereferencing:
```sinistra
@verb = "look";
@arg = 42;
if sys.exists{&command.[@verb]} then
  command.[@verb]{@arg};
else
  net.write{input.line, "No such command.\n"};
endif;
```
This works because we have a known structure with two well-defined variables:
- the name of the command itself, and
- the argument to pass to the command.
We first test to see if the command exists by constructing an item-reference
using a dereferenced local variable (in this case, `command.[@verb]` resolves to
`command.look`) and then call the command with its argument.  This is a common
pattern, and simple to understand and implement.

But what if we need to write a handler dispatcher which knows neither the
target item path nor the number of arguments to pass to it?  A simple
dispatcher as above would simply not work in this scenario, so we need to use
`sys.call`:

```sinistra
handlers.arrive = code {@player, @room} (
  sys.log{"Arrival handler called.\n"};
);

event.handler = &handlers.arrive;
@handler = event.handler;
@arguments = #[42, 7];

if sys.exists{@handler} then
  sys.call{@handler, @arguments};
else
  sys.log{"Handler not installed.\n"};
endif;
```
In this example, `event.handler` has been set with the item-reference of
`handlers.arrive`, but it could just as easily have been something like
`room.triggers.door.handles.arrive`, or something even more arcane. Similarly,
the arguments list could be empty or contain a dozen different values. sys.call
simply supplies those list elements as arguments to the target code item; the
ordinary Sinistra rules for missing and excess arguments then apply.

Notice that sys.exists{@handler} only establishes that the referenced item exists; it does not establish that the item contains code. If the reference resolves to a value item, sys.call returns nil.

## Runtime Compilation
A Sinistra world can extend itself at runtime.  It does this by means of the
`sys.compile` libcall.
```sinistra
@source = """
greetings.hello = code {@name} (
  sys.log{"Hello, "};
  sys.log{@name};
  sys.log{"!\n"};
);
""";

if sys.compile{@source} then
  sys.log{"New greeting installed.\n"};
else
  sys.log{"Compilation failed:\n"};
  sys.log{error.msg};
  sys.log{"\n"};
endif;
```

You can see that `sys.compile` accepts a string containing Sinistra source
code. It compiles that source into a temporary code item, executes it, and then
removes the temporary item. Any changes made by the compiled source — such as
creating greetings.hello above — remain in the item tree.

If compilation and execution complete successfully, `sys.compile` returns
`true`. Otherwise it returns `false` and leaves the `error` item populated with
(hopefully useful) information about what went wrong.

*Warning*: This has been said before, but it bears repeating: One restriction is
that code which is currently executing is pinned and cannot be replaced or
deleted until that execution has finished.

This example also demonstrates the raw string delimiter, `"""..."""`.
Everything between the delimiters is copied into the string exactly as written:
backslashes do not introduce escapes, and literal tabs and newlines are
preserved. The one obvious limitation is that the exact sequence """ cannot
occur inside a raw string, because it terminates it. Raw strings are
particularly useful when passing substantial pieces of Sinistra source to
`sys.compile`, since otherwise every quote and backslash in the embedded source
would need another layer of escaping.

Once `sys.compile` has returned successfully, `greetings.hello` is an ordinary
code item and can be called like any other.  It will be persisted with the rest
of the tree on the next normal save operation.
```sinistra
greetings.hello{"George"};
```

## Walking the Item Tree
With dynamic expansion of the world, there will come a time when you are not
entirely sure just what is in your item tree.  There are four `sys` libcalls
which will help in this situation.  The first two are demonstrated in the
snippet below (which you could add to your First World if you still have it):
```sinistra
commands.rootname = code{@l, @args} (
  @c = 0;
  @count = sys.rootcount;
  net.write{@l, "Root items:\n"};
  while @c < @count do
    @i = sys.rootname{@c};
    net.write{@l, @i};
    net.write{@l, "\n"};
    @c++;
  endwhile;
  net.write{@l, "End of list\n"};
  net.write{@l, "There are "};
  net.write{@l, @count};
  net.write{@l, " root items.\n"};
);
```
Two libcalls are demonstrated here: `sys.rootcount`, which returns the number
of root items (i.e. the top-level items in the root namespace), and
`sys.rootname`, which returns the name of the root item at a given index.  In
the first world, this will give an output similar to:
```
archaro@stick:~/sin$ telnet localhost 4001
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
Connected.
Hello!  You are on line 0
rootname
Root items:
error
input
docommand
rooms
commands
greetings
player
End of list
There are 7 root items.
```
*Important Note*: The precise order of the names is not significant and should
not be relied upon across runtime restarts.

The other two libcalls operate below the root level. sys.childcount returns the
number of immediate children of a given item, while sys.nthname returns the
layer name of the child at a given zero-based index. Together they perform the
same job for an arbitrary item that sys.rootcount and sys.rootname perform for
the root namespace.

By combining these four libcalls it is relatively straightforward to walk the
entire item tree recursively. This is left as an exercise to the reader.

Kidding!  Here it is:
```sinistra
commands.rootname = code{@l, @args} (
  @c = 0;
  @count = sys.rootcount;
  net.write{@l, "The Item Tree:\n"};
  while @c < @count do
    @i = sys.rootname{@c};
    net.write{@l, @i};
    net.write{@l, "\n"};
    if sys.childcount{@i} > 0 then
      walktree{@l, @i, 2};
    endif;
    @c++;
  endwhile;
  net.write{@l, "End of list\n"};
  net.write{@l, "There are "};
  net.write{@l, @count};
  net.write{@l, " root items.\n"};
);

walktree = code {@l, @start, @indent} (
  @c = 0;
  @count = sys.childcount{@start};
  while @c < @count do
    @layer = sys.nthname{@start, @c};
    @i = 0;
    while @i < @indent do
      net.write{@l, " "};
      @i++;
    endwhile;
    net.write{@l, @layer};
    net.write{@l, "\n"};
    @item = @start + "." + @layer;
    if sys.childcount{@item} > 0 then
      walktree{@l, @item, @indent + 2};
    endif;
    @c++;
  endwhile;
);
```
This example also rather neatly demonstrates recursion.  Each call to
`walktree` tests to see if the item under examination has any children.  If it
does, the fully-qualified name of each item is constructed and passed
recursively to `walktree` to examine the next layer down.

## Tasks
Tasks schedule separate executions of Sinistra code through the event loop.
They do not create threads: Sinistra code remains serialised on the single
event-loop thread. To prevent this document from becoming unwieldy, the full
discussion of tasks is [here](tasks-and-events.md).
