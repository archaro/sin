# How to be Sinister - Your First World

## What we are going to do
Now you have seen something of Sinistra's concepts, we are going to create
something which might reasonably be called a Sinistra world.  It will be small,
but beautiful.  When it is complete, you might use it as the base on which to
build something more substantial.

By the end of this tutorial, you will have a world consisting of one room
(small but beautiful, remember?) and you will be able to put a player into it
who will be able to `look` at it.

This is the initial item tree which our loader will create:
```
.
├── rooms
│   └── 1
│       ├── name
│       └── desc
├── commands
│   ├── look
│   ├── quit
│   └── shutdown
├── input
└── docommand
```

## Before we start
Tidiness is a good habit to cultivate, so let's create this world in its own
directory.  That way, if you decide that it is too awful to exist, you can
simply delete the directory and pretend that it never was.

At this point, we are assuming that you have a terminal window open, and that
you are in the directory where you built the executables.  Create a new
directory in $HOME, and copy the executables there (we'll copy all of them,
but we will only use two):
```bash
mkdir ~/firstworld
cp sin scomp sdiss sconv ~/firstworld
cd ~/firstworld
```

Now, we shall begin.

## Creating the loader

We need to create the world but, as we know, this is quite easy.  Open your
favourite text editor (hint: it's vim) and create the file `fw-load.src`:

```sinistra
/* NB: comments are delimited like this - the compiler ignores them */

/* Here is our room */
rooms.1.name = "The Beginning";
rooms.1.desc = "You are standing somewhere remarkably small.";

/* Now some commands - they all receive the line of the current player in @l */
commands.look = code {@l} (
  /* These are local variables.  They aren't strictly necessary here, but
     now you know how to use them
   */
  /* The dereference operator `[]` means "substitute the content of this" */
  @r = player.[@l].room;
  net.write{@l, rooms.[@r].name};
  net.write{@l, "\n"};
  net.write{@l, rooms.[@r].desc};
  net.write{@l, "\n"};
);

commands.quit = code {@l} (
  net.write{@l, "Goodbye!"};
  net.write{@l, "\n"};
  net.ditch{@l}; /* Drain pending output and drop the connection. */
);

commands.shutdown = code {@l} (
  /* Perform an orderly shutdown - this persists the item tree to disk
     Note that we ignore @l here.
   */
  sys.log{"Normal shutdown.\n"};
  sys.shutdown;
);

/* And the input item - the interface between the player and the world */
input = code (
  @a = net.input; /* This returns an integer between 0 and 3 */
  if @a == 1 then
    /* This is a new connection.
       input.line is the zero-based connection slot assigned by net.input
       The input item defaults to `input` and `net.input` manages sub-items
       underneath.
     */
    @l = input.line;
    sys.log{"New connection on line "};
    sys.log{@l};
    sys.log{".\n"};
    /* Send a cheery greeting to the new connection */
    net.write{@l, "Hello!  You are on line "};
    net.write{@l, input.line};
    net.write{@l, "\n"};
    /* Now we construct the player state.  Remember how commands.look
       referenced player.[@l].room?  This is where that item is set.
     */
    player.[@l].room = 1; /* The starting (and currently only) room */
  elsif @a == 2 then
    /* This is a disconnection. */
    @l = input.line;
    sys.log{"Line "};
    sys.log{@l};
    sys.log{" has disconnected.\n"};
    /* Note the use of & to point to the item itself and not its contents */
    sys.delete{&player.[@l]}; /* Remove the disconnected player's state */
  elsif @a == 3 then
    /* The player has sent some data - process it.
       input.text contains the data sent by the player
     */
    docommand{input.line, input.text};
  endif;
  /* If @a == 0 there was no network event to process. */
);

docommand = code {@l, @cmd} (
  /* docommand receives two parameters - the line number of the current player
     and the text sent by them. This is really naive, but it serves as an
     illustration.  Note that if an item doesn't receive all the parameters it
     expects, the missing ones are assumed to be `nil`.  Extra parameters are
     silently dropped.
   */
  if sys.exists{&commands.[@cmd]} then
    /* This libcall checks to see if an item exists. */
    commands.[@cmd]{@l};
  else
    net.write{@l, "I beg your pardon?\n"};
  endif;
);
```

And that's it.  Save it, because we will compile it in a moment.  First, we
also need to create the bootstrap code.  Create another text file called
`fw-boot.src`:
```sinistra
sys.log{"Starting the world...\n"};
/* Because the input item manages the player item and all its children,
   we delete it on startup so that we start from a clean slate.
 */
sys.delete{&player};
```

Save that, and then compile both:
```bash
./scomp fw-load.src fw-load.obj
./scomp fw-boot.src fw-boot.obj
```

If you have created these files exactly as above, you will now have two
bytecode files: `fw-load.obj` and `fw-boot.obj`.  If the compiler didn't like
something, it will stop and complain bitterly - but it will try to explain what
the problem is.  *Make sure that you copy the above source exactly!*

Next, you need to load your world into the itemstore:
```bash
./sin --loadonly -o fw-load.obj
```

There is one detail here which may not be immediately obvious: `scomp` compiles
the loader itself, but the source bodies of any code(...) items created by that
loader are carried inside the object file and compiled when the loader is
executed by `sin`. Consequently, an error inside one of those code bodies may
not be reported by `scomp`; it may instead appear during the `--loadonly` step.

The same underlying mechanism is used when code items are created dynamically
in a running world. `sys.compile` packages the compilation and execution
process together and reports failure if either stage fails.

## Starting the world and connecting to it

Having loaded the initial item tree, all that remains is to run it:
```bash
./sin -o fw-boot.obj
```

You should see something like this:
```
Bytecode loaded: 53 bytes from fw-boot.obj.
Using 'srcroot' as the source root.
Runtime options: loadonly=0 strict_validation=0 strict_runtime_contracts=0.
Loading itemstore from items.dat.
Starting the world...
Using `input` as the input item.
Listening on port 4001.
Running...
```

Now open another terminal window and telnet to localhost:4001
```
archaro@stick:~/sin$ telnet localhost 4001
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
Connected.
Hello!  You are on line 0
```

Congratulations!  You have connected to your first world!  Type `look` and see
what happens.
```
look
The Beginning
You are standing somewhere remarkably small.
```

That is the room description you loaded.  So what has happened is that Sinistra
has connected you to your world on line 0, created your game state by setting
the item `player.0.room` to 1 (the first and only room), and then when you
typed `look` it dereferenced that item in order to access `rooms.1.name` and
`rooms.1.desc`.

If you like, you can open a second terminal window and connect, and you will
see that you are on line 1.

The item tree now looks something like this:
```
.
├── rooms
│   └── 1
│       ├── name
│       └── desc
├── commands
│   ├── look
│   ├── quit
│   └── shutdown
├── input
│   ├── line = 0 (or 1, depending on which connection last had activity)
│   └── text = "look"
├── docommand
└── player
    ├─ 0
    │  └── room = 1
    └─ 1
       └── room = 1
```

Now type `shutdown` in one of the connections to do an orderly shutdown. In the
terminal window which is running `sin` you will see this:
```
Normal shutdown.
Sys.shutdown called.  Shutting down.
Shutting down.
Line 1: 127.0.0.1 disconnected.
Line 0: 127.0.0.1 disconnected.
```

`items.dat` will have been updated to persist the current item tree, which
means that it now contains the `player` item and its children, and the children
of the `input` item.  This is why we delete the `player` item in the bootstrap.

## Extending the world in real-time.

Sinistra allows the item tree to be modified while it is running, and that
includes code items (remembering that a currently-executing item is pinned and
cannot be replaced or deleted).  We're going to do that next, but first we need
to add another command to the `commands` item.  Create another text file with
this content, and then compile it with `scomp` as you did the other two files:

Note one important peculiarity here: in the modified `docommand` we are now
passing *two* arguments to the identified command item, but the existing
command items have been defined as only taking one.  Have no fear: by default,
Sinistra silently drops excess arguments.

```sinistra
commands.compile = code {@l, @args} (
  /* This command is an interface to sys.compile.  It is dangerous!  In a real
     world, it would be protected with access control (for example, restricting
     its use to wizards only).  DO NOT USE THIS IN A REAL PROJECT!
   */

   @c = sys.compile{@args}; /* Compile the source.  Returns true or false. */
   if @c then
     net.write{@l, "Source compiled.\n"};
   else
     net.write{@l, "Compilation failed.  Compiler error:\n"};
     net.write{@l, error.msg};
     net.write{@l, "\n"};
   endif;
);

docommand = code {@l, @line} (
  /* docommand receives two parameters - the line number of the current player
     and the text sent by them. This is really naive, but it serves as an
     illustration.  Note that if an item doesn't receive all the parameters it
     expects, the missing ones are assumed to be `nil`.
   */
  @line = str.trim{@line};
  @p = str.find{@line, " "};

  if @p == -1 then
    @verb = @line;
    @args = "";
  else
    @verb = str.substr{@line, 0, @p};
    @args = str.substr{@line, @p + 1, str.len{@line} - @p - 1};
    @args = str.trim{@args};
  endif;

  if sys.exists{&commands.[@verb]} then
    /* This libcall checks to see if an item exists. */
    commands.[@verb]{@l, @args};
  else
    net.write{@l, "I beg your pardon?\n"};
  endif;
);
```

We cannot add commands.compile to the already-running world yet, because we do
not have any in-world mechanism for compiling new source. So we shall stop the
world (if it is currently running), install this command with --loadonly, and
restart it. From that point onwards, we can make changes while the world is
running.

```bash
./scomp fw-compile.src fw-compile.obj

# GRAVE WARNING: Only ever run `sin --loadonly` on a quiesced world.
# First shut down the world if it is still running, then:
./sin --loadonly -o fw-compile.obj

# Then restart with:
./sin -o fw-boot.obj
```

The world now contains the compile command, which you may use hereafter for
*almost* all your creative needs.  Always remember that you cannot replace
pinned (i.e. executing) code.  For that, you may need to do the
`sin --loadonly` tango.

## Since we have been given `compile`, let us enjoy it

You don't have to be a Borgia to enjoy your new-found power but, if you are,
it helps.  Now the world is running again, telnet to it and type `look`:

```
archaro@stick:~/sin$ telnet localhost 4001
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
Connected.
Hello!  You are on line 0
look
The Beginning
You are standing somewhere remarkably small.
```

Now change the description of room 1:
```
compile rooms.1.desc = "You are standing somewhere remarkably small. It smells faintly of purple.";
```
Then `look` again.

```
compile rooms.1.desc = "You are standing somewhere remarkably small. It smells faintly of purple.";
Source compiled.
look
The Beginning
You are standing somewhere remarkably small. It smells faintly of purple.
```
Behold!  You have changed the room description. If you want to prove that this
is not merely an ephemeral change, type `shutdown` now. This will persist the
item tree to disk and end the world (as 'twere). Restart it as before,
reconnect, and `look`: the new description will still be there.

## But wait, there's more

Compilation is not limited to changing the values of simple items.  We can
create whole new code items.  Type this:
```
compile commands.hello = code {@l} ( net.write{@l, "Hello from newly-created code!\n"}; );
```
Then type `hello`.

```
compile commands.hello = code {@l} ( net.write{@l, "Hello from newly-created code!\n"}; );
Source compiled.
hello
Hello from newly-created code!
```

See?  You just extended the world in real-time by creating a new command under
the `commands` item.

## Into every life a little rain must fall

You will not get every compilation right first time.  Sometimes the compiler
will look at your hopeful face, think for a moment, and then give you an `F`.

The `sys.compile` libcall returns `false` if compilation fails, in which case
the `error` item will be populated with several child items, each of which will
give you a small insight into what exactly the problem was.  These sub-items
are:
- `error.msg` - complete formatted diagnostic
- `error.code` - stable compiler diagnostic code
- `error.stage` - e.g. PARSE, SEMANT
- `error.file` - source file name (or "<memory>" if compiling from a string)
- `error.line` / `error.column` - reported location of the error
- `error.excerpt` - offending source line
- `error.item` - normally nil for a direct compiler diagnostic - ignore for now

The `compile` command in your first world wraps up `error.msg` neatly, giving
this output:
```
richard@stick:~/sin$ telnet localhost 4001
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
Connected.
Hello!  You are on line 0
compile foo = 10
Compilation failed.  Compiler error:
SIN-PARSE-0001 stage=PARSE file=<memory> line=1 column=7 message=<memory>: syntax error excerpt=foo = 10
```

Can you see the error?  There is a semicolon missing at the end of the
assignment statement.  Sinistra likes its semicolons.

## In conclusion: What You Have Built

You have created your first world.  It is tiny, but it is a real Sinistra
world. Its rooms, commands and player state are all items in the same tree.
Some contain ordinary values; others contain executable code. The tree is
persistent, but parts of it may be deliberately transient. The running world
can modify that tree, including its own executable behaviour.

Nothing especially MUD-like is built into Sinistra itself. Rooms, players,
commands and the command parser exist because you created them. A different
game, a different application, could organise the same ideas into an entirely
different item tree.

Your next mission, should you choose to accept it, is to return to the
[guide index](index.md) and look through the other documents, which go into
more detail about Sinistra features and concepts.  You might also enjoy the
[reference section](../reference/), which is somewhat drier and more technical,
and is intended to give you specific answers to specific questions.

Then, perhaps, you could expand your first world.  Perhaps a second room?  And
then a means of moving 'twixt the two?  The possibilities are (mostly) limited
by your imagination.
