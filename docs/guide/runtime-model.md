# How to be Sinister - How Sinistra is Sinister

Sinistra is a single-threaded, event-driven persistent runtime engine. It loads
the item tree into memory, executes a bootstrap object once, and then services
network activity, the configured input item, and timed tasks through one event
loop. Sinistra code runs synchronously: while one code item is executing, no
other Sinistra code executes concurrently with it.

```
startup
   |
   v
load item tree
   |
   v
execute bootstrap once
   |
   v
event loop ----+
               |
             +-+------------------------------------------------+
             | |                                                |
             | +---- input timer -------> input ----> net.input |
             | |                                                |
             | +---- task timer --------> task code             |
             | |                                                |
             | +---- network callbacks -> queue network events  |
             | |                                                |
             +-+------------------------------------------------+
               |
               v
            shutdown ---> normal shutdown? -+-Y--> save item tree -+
                                            |                      |
                                            N                      |
                                            |                      |
                                            +----------+-----------+
                                                       |
                                                       v
                                               end of execution
```

## Starting the World

The runtime engine executable is `sin`.  It has several options, but only the
critical ones are described here - the Reference Manual is where to go if you
want all the details.

In order to start your world, at the *very least* you require some object
code compiled with `scomp`.  For an initial world, this will probably include
some basic items and item tree structure which you will want to load before
doing anything else.  When you run the live world, this object code will
usually be your bootstrap object.  Remember your First World: first you
compiled the `*-load.src` file with `scomp` and ran it through `sin` like this:
```bash
./sin --loadonly -o chat-load.obj
```
Next, assuming that completed successfully, you opened the world to the network
by compiling and then running the bootstrap code:
```bash
./sin -o chat-boot.obj
```
And these are the only two options you really need to get started.  When you
run `sin` it looks for `items.dat` in the current working directory.  If the
file is found, it loads the item tree into memory from there, otherwise it
creates a new `items.dat` file when it performs a normal shutdown.

The file you supply with option `-o` is compiled Sinistra bytecode.  This is
executed once only.  While the code in the object file may create or modify
items, the object file itself is not installed in the item tree.  This allows
you to replace the bootstrap object easily, which can be very handy if you
need to go into "rescue mode" and bypass your usual internal world startup
process.  In ordinary network mode this externally-supplied object normally
serves as the bootstrap.

Once the object file has been processed `sin` normally enters the event loop,
unless `--loadonly` is specified: in which case the runtime engine will exit
after running the object code, persisting the resulting item tree to
`items.dat`.

Now is as good a time as any to restate that the code in the object file is
*not necessarily* the `input` item code - although it may contain code which
creates (or recreates) the `input` item in the item tree.

## One Event Loop, One Sinistra Execution

This is not the place to go into the internals of the event loop, but it is
helpful to know *something* about how it operates, in order to play nicely with
it.

The event loop is handled by `libuv`, a fast and feature-rich event library
used by, amongst other things, `Node.js`.  Sinistra code is serialised on this
loop: code items do not run simultaneously, even when one item calls another.
In other words, code item calls are synchronous; the caller waits until the
callee returns.

Even task callbacks are not threads, even if they might look as if they are.
Rather, a task is given a timer and the event loop notices when the timer has
become eligible and runs the task code.  Network callbacks are handled in a
conceptually similar manner - they do not suddenly cause Sinistra code to
execute concurrently.

This has a practical consequence:

*A long-running Sinistra item does not merely delay itself: it prevents the
event loop from servicing other events until it returns.*

From which we derive the cardinal rule:
***NO PERPETUAL LOOPS.***

And the slightly broader rule:
***Code which is entered from the event loop should do its work and return.***

Do these things that you might live long in the land.

If you are interested in *how* this is effected in the code, you will want to
look at the [internals](../internals/) documentation.

## The `input` Item and Network Events

The `input` item is invoked from a repeating timer with a nominal 10 ms
interval. “Nominal” matters: this is not a real-time scheduling guarantee. If
something else is executing, its next invocation is delayed.

The diagram at the top of this document has this:
```
+---- input timer -------> input ----> net.input
```
Do not be misled into thinking that the input item *automatically* calls
`net.input`.  It does not.  It is, however, the logical place for `net.input`
to be called, although precisely *where* in your `input` code this happens is a
matter for your world design.

When `net.input` is called, it is guaranteed to return an integer between `0`
and `3`, signifying what has happened:
- `0`: There are no pending network events
- `1`: There is a new connection
- `2`: A connection has disconnected
- `3`: A complete line of input has been received

For event types `1`, `2`, and `3`, `net.input` sets the configured input-line
item (normally `input.line`) to the connection number. For event type `3`, the
configured input-text item (normally `input.text`) contains the received line
of input, minus its terminating newline.

Remember that `net.input` processes at most one queued network event per
invocation. If several events are waiting, later invocations deal with the
rest.  This is an important conceptual point: do not assume that `net.input` is some sort of blocking socket reader: it isn't.

## Calls, the Call Stack, and Pinning

Code items call other code items synchronously, creating a call stack.  When
an item begins execution, it is pinned until execution completes.  This means
that you can have several items pinned at once:
```
input (item)
  └─> docommand (item)
        └─> commands.compile (item)
              └─> sys.compile (libcall)
```

The above diagram shows how a user might issue a compilation command while
connected to the world.  The user input starts off in `input`, gets passed to
`docommand` for processing, and finally `commands.compile` is called, which
actually handles the call to `sys.compile`.  Each item is pinned when it is
executed, and the pin is not removed until that frame returns.

The practical consequence of this is that a pinned item cannot be replaced or
deleted. Nor can an item be deleted if its subtree contains a pinned item.
Thus an executing call chain cannot pull itself apart from underneath.

Sinistra is highly mutable: it is *not* self-modifying underneath an active
stack frame.

In a Sinistra world, values can change; items can be created and deleted; even
code items can be installed or replaced almost at will.  Through `sys.compile`
new Sinistra source code is compiled and executed synchronously. Mutations to
unpinned items take effect immediately, and subsequent accesses see the altered
tree. The exception is an item which is currently pinned: it cannot be replaced
or deleted until its execution has completed.  To attempt to do so would be a
little like extending a staircase by removing the bottom steps and adding them
to the top.

A mental model might make things clearer:
```
call item A
    |
    v
execute the version of A currently installed
    |
    v
return
    |
    v
A may now be replaced
    |
    v
next call sees the replacement
```

## Timed Tasks

This is a brief overview of a major subsystem.

Tasks can be created dynamically within Sinistra.  These tasks are attached to
the same event loop as the input item, and operate in the same way: when
eligible, a task's code is called synchronously and the event loop waits until
the task completes.  Remember, *Sinistra is not concurrent*.

Sinistra is also not a real-time system.  The start and repeat intervals for
tasks describe eligibility, not hard deadlines - a busy event loop can make a
task late.  While, conceptually, a world author might think of a task as
"running in the background", *this is not what actually happens*.  Ensure that
your task code returns reasonably promptly.

For a full discussion of tasks, read the [tasks and events guide](tasks-and-events.md).

## Memory, Persistence, and Shutdown

While Sinistra is running, the in-memory item tree is the live world. The file
on disk is its persisted representation, not the thing against which
individual language operations execute.

- The itemstore is loaded at startup and becomes the item tree;
- Runtime mutations change this tree in-memory;
- Executing `sys.save` checkpoints the in-memory tree back to the itemstore;
- Executing `sys.backup` writes the item tree to a separate, time-stamped
  backup itemstore.
- Executing `sys.shutdown` requests a normal shutdown, during which the
  in-memory tree is persisted to disk.
- Executing `sys.abort` exits the runtime without the normal save.

Remember, too, that the source text associated with code items is stored
separately under `srcroot/`.  Value items have no source sidecar.

From this we derive another important rule: *Do not run a second instance of
`sin` against the same `items.dat` or `srcroot/`.*  Such a foolhardy action
would cause nothing but regret and misery.

## Four Things to Remember

1) Sinistra is synchronous - items are executed one at a time.
2) Code items cannot be replaced or deleted while they are pinned.
3) Tasks are scheduling opportunities, not real-time guarantees.
4) While `sin` is running, the in-memory item tree is authoritative.

