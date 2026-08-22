# How to be Sinister - An Overview

## So what's it all about, then?

Sinistra is a small persistent runtime built around an in-memory tree of named
items. Items may contain data or executable code. The runtime loads that world
into memory, executes code against it, accepts external events, and writes the
resulting state back to persistent storage.

The language, compiler, bytecode VM, networking layer and persistence system
all exist in service to this model.  Think of it as the misbegotten offspring
of FORTH and Smalltalk.

## An in-memory who of the what now?

An in-memory tree of named items.  There is a root item (unnamed, but
conceptually referred to as `.`), under which top-level items can be created.
These, in turn, may have child items, which may also have child items, and so
on.  The namespace is formed by the item path, separated by dots.  Items may
contain values or they may contain code - they are not 'variables' in the sense
of an ordinary programming language: the item tree is simultaneously a data-
structure, a namespace, and a programmable world state.

A tiny schematic will, hopefully, make things clearer:
```
. (root)
├── commands
│   ├── version
│   └── shutdown
├── players
│   ├── dave
│   │   ├── name
│   │   └── location
│   └── doris
└── input
```

We can address these items by using item notation:
```sinistra
players.dave.name = "Sensible Dave";
players.dave.location = "kitchen";
```

## Code is part of the item tree, too

Let's create a bit of Sinistra code and assign it to an item called `greet`
(don't worry about *how* this happens right now, it will be explained in
detail elsewhere):

```sinistra
greet = code {@name} (
  sys.log{"Hello "};
  sys.log{@name};
);
```

If `greet` is then called by another item (like this: `greet{"Dave"}`) then the
item will be executed (in this case, it writes "Hello Dave" to the system log).

So we talk about *code items* rather than *functions* because they are simply
a type of item which executes code rather than storing a value.  Code items can
also return a value, too, so you can have something like this:
```sinistra
greet = code {@name} (
  sys.log{"Hello "};
  sys.log{@name};
  return 1;
);

greet.count = 0;
sys.log{greet.count};
greet.count = greet.count + greet{"Dave"};
sys.log{greet.count};
```

This will output the value of `greet.count` (initially 0), then execute `greet`
and add it to the current value of `greet.count`.  When `greet.count` is
output again, it will have the value 1.

## Code starts as source, but is compiled to bytecode
The bootstrap code (more later) is compiled separately using the `scomp` tool.
Other code items may be compiled using this tool, or dynamically from within
the language by use of the `sys.compile` libcall.  The compilation process
produces bytecode which is then executed by the VM within the Sinistra runtime
(`sin`).

Although the bootstrap code is always separate, other code items will be
inserted into the item tree.

## And the compiler is inside the runtime

The compiler is not only a standalone tool. Running Sinistra code can invoke it
through `sys.compile`, passing source text to be compiled while the world is
live. That source can create new code items or replace existing ones.

This means that the distinction between developing the world and running the
world is deliberately blurred: the running system can extend its own behaviour
without being shut down and rebuilt.

## Uh, "bootstrap"?

A Sinistra world is dynamic: items can be created, deleted or modified at
runtime - even code items.  To prevent the universe from imploding, it is not
possible to delete or modify a code item which is currently being executed.

The bootstrap code is slightly different from ordinary code items. It is
supplied to `sin` as an external object file and executed once when the runtime
starts. It is not installed in the item tree merely by virtue of being the
bootstrap object.

Once bootstrap execution has finished, `sin` enters its event loop. By default
it invokes the persistent code item `input` repeatedly, at a nominal interval of
10 ms. Like any other code item, `input` cannot be replaced while a particular
invocation is executing, but it is not permanently pinned.

## A note on persistence

Persistence means "memory first, disk second".  The full item tree is loaded
into memory when the runtime starts up, and is saved on normal exit.  It may
also be saved by calling the `sys.backup` libcall.  The on-disk store is
called `items.dat`, and contains the persistent item tree, including values and
compiled bytecode. The corresponding source text for code items is stored
separately beneath `srcroot/`.

## Average minds discuss events

Sinistra is event driven, which allows for timed and repeatable tasks as well
as network activity. In network mode, `sin` invokes the input item repeatedly
at a nominal interval of 10 ms. The `input` item normally calls `net.input`,
which processes at most one outstanding network event. Timed Sinistra tasks are
scheduled separately on the same libuv event loop and run when their timers
become eligible.

All of this runs serially on the event-loop thread: one Sinistra code item is
not executing concurrently with another.

## What Sinistra is not

- It is not a conventional executable program with an embedded database.
- It is not merely an embedded scripting language.
- It is not OO (at least not in the conventional, class/instance sense).
- It is not FORTH (but its ancestry should be obvious to anyone whose teeth are long enough).
- It is not Smalltalk (but there are clear echoes in the idea that code items are modifiable within the runtime system).

## Putting it all together

```
source → scomp → bootstrap/setup object
                    |
                    v
                  sin
                    |
                    v
             in-memory item tree
              /             \
       network/tasks      sys.compile
              \             /
               world changes
                    |
                    v
                 items.dat
```

Because Sinistra does not come with batteries included (in fact, it comes with
a copy of the Periodic Table and a slightly-stained guide to the creation of
new universes), `sin` accepts the `--loadonly` option, which will process
a bytecode object file without immediately going into network mode.  This
makes it easy to populate the item tree before actually letting people in.

## If you are still here...

Your next point of call should probably be the guide.  Here is the [index](index.md).

