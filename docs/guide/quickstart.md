# Quickstart

## Dependencies
The development environment is Ubuntu 24.04, but any modern Linux distro should be fine, as long as libuv1 is available (version 1.48 definitely works, but any recent version should be good).

The code is written for x86_64 - other architectures are not supported but PRs are welcome to enable them.

## Build

```bash
make
```

This builds:

- `sin` (runtime engine)
- `scomp` (standalone compiler)
- `sdiss` (standalone disassembler)
- `sconv` (itemstore converter)

## Run the chat server example

The chat server is a simple example of Sinistra in action, so this walkthrough
uses the chat source files consistently:

- [docs/guide/examples/chat-load.src] creates the persistent `input` code item that handles
  connections, disconnections, and incoming text.
- [docs/guide/examples/chat-boot.src] is the startup code that runs each time the server is
  launched.

From the repository root, compile each source file to bytecode object code:

```bash
./scomp docs/guide/examples/chat-load.src chat-load.obj
./scomp docs/guide/examples/chat-boot.src chat-boot.obj
```

`./scomp <source> <object>` reads the Sinistra source file at `<source>` and
writes compiled bytecode to `<object>`. These object files will be processed by
`sin` - the first one will be run once to initialise the itemstore, while the
second is the boot item, which will be run when the chat server starts up.

Initialize the itemstore once by running the load object in load-only mode:

```bash
./sin --loadonly -o chat-load.obj
```

Use `./sin --loadonly -o ...` when you want to execute an object file and then
exit without opening the network listener. This is useful for one-time setup,
such as creating or updating persistent items in the itemstore. The command
above creates or updates the default itemstore in the current directory. It
creates `items.dat`, which is the on-disk itemstore (read into memory whenever
`sin` starts up, and written on backup or normal shutdown), and `srcroot/`,
which stores the source of code items when they are created.

Start the server with the boot object:

```bash
./sin -o chat-boot.obj
```

Use `./sin -o ...` *without `--loadonly`* when you want the engine to start
normally and accept network connections.

## Connect to the server

By default the listener uses port `4001`. In another terminal, connect with
either telnet or netcat:

```bash
telnet localhost 4001
```

Open a second client to see the chat behavior: each client receives notices when
another line connects, sends text, or disconnects. To use a different port, pass
`-p <port>` when starting the server, for example:

```bash
./sin -o chat-boot.obj -p 4002
```

## Shut down safely

A safe shutdown is one that lets the engine leave the run loop normally and save
the in-memory itemstore back to `items.dat`. Sinistra code does that by calling
`sys.shutdown;`.

The chat server has two commands - both prefixed with `\`:
- \quit - logs you out
- \shutdown - shuts the server down

## Restart from saved state

After the one-time load-only run has created `items.dat`, restart the server
from saved state with only the boot object:

```bash
./sin -o chat-boot.obj
```

Do not rerun the `./sin --loadonly -o chat-load.obj` initialization
step unless you intentionally want to reload or replace the persistent `input`
item from `docs/guide/examples/chat-load.src`.
