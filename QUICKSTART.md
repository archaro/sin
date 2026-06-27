# Quickstart

## Dependencies
The development environment is Ubuntu 24.04, but any modern Linux distro should be fine, as long as libuv1 is available (version 1.48 definitely works, but any recent version should be good).

The code is written for x86_64 - other architectures are not supported but PRs are welcome to enable other architectures.

## Build

```bash
make
```

This builds:

- `sin` (runtime engine)
- `scomp` (standalone compiler)
- `sdiss` (standalone disassembler)

## Run the chat server example

The chat server is the more complete example, so this walkthrough uses the chat
source files consistently:

- `examples/chat-load.src` creates the persistent `input` code item that handles
  connections, disconnections, and incoming text.
- `examples/chat-boot.src` is the startup code that runs each time the server is
  launched.

From the repository root, compile each source file to bytecode object code:

```bash
./scomp examples/chat-load.src examples/chat-load.obj
./scomp examples/chat-boot.src examples/chat-boot.obj
```

`./scomp <source> <object>` reads the Sinistra source file at `<source>` and
writes compiled bytecode to `<object>`. The `.obj` files are inputs for the
runtime engine; they are not the saved game state.

Initialize the itemstore once by running the load object in boot-only mode:

```bash
./sin -b -o examples/chat-load.obj
```

Use `./sin -b -o ...` when you want to execute an object file and then exit
without opening the network listener. This is useful for one-time setup, such as
creating or updating persistent items in the itemstore. The command above creates
or updates the default itemstore in the current directory.

Then start the server with the boot object:

```bash
./sin -o examples/chat-boot.obj
```

Use `./sin -o ...` without `-b` when you want the engine to execute the object
file and then continue into the normal run loop. In this mode the engine starts
the input task, opens the network listener, and accepts client connections.

## Files created by the walkthrough

The commands above create these files/directories in the current working
directory:

- `examples/chat-load.obj`: bytecode produced from `examples/chat-load.src`.
- `examples/chat-boot.obj`: bytecode produced from `examples/chat-boot.src`.
- `items.dat`: the saved itemstore. It contains the persistent item tree,
  including the `input` code item created by `examples/chat-load.src`.
- `srcroot/`: the source root directory used by the runtime. If `./sin` is run
  without `-s <dir>`, it uses `./srcroot` and creates it if it does not already
  exist. Source text for saved code items may be written under this tree.

If you want to keep example state out of the repository root, run the `./sin`
commands from a separate working directory and pass paths back to the compiled
objects, or use `-i <itemstore>` and `-s <srcroot>` to choose different state
locations.

## Connect to the server

By default the listener uses port `4001`. In another terminal, connect with
either telnet or netcat:

```bash
telnet localhost 4001
```

or:

```bash
nc localhost 4001
```

Open a second client to see the chat behavior: each client receives notices when
another line connects, sends text, or disconnects. To use a different port, pass
`-p <port>` when starting the server, for example:

```bash
./sin -o examples/chat-boot.obj -p 4002
```

## Shut down safely

A safe shutdown is one that lets the engine leave the run loop normally and save
the in-memory itemstore back to `items.dat`. Sinistra code does that by calling
`sys.shutdown;`. Add that call to an administrative command or shutdown task in a
real application.

The chat quickstart does not include an administrative shutdown command, so use
`Ctrl-C` only as a development stop for the sample server. If the running server
has important in-memory state that is not already saved, prefer a `sys.shutdown;`
path instead of interrupting the process. Avoid `sys.abort;` for routine
shutdowns because it skips the normal itemstore save.

## Restart from saved state

After the one-time boot-only load has created `items.dat`, restart the server
from saved state with only the boot object:

```bash
./sin -o examples/chat-boot.obj
```

Do not rerun the `./sin -b -o examples/chat-load.obj` initialization step unless
you intentionally want to reload or replace the persistent `input` item from
`examples/chat-load.src`.
