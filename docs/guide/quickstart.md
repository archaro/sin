# How to be Sinister - Quickstart

## Dependencies
The development environment is Ubuntu 24.04, but any modern Linux distro should be fine, as long as libuv1 is available (version 1.48 definitely works, but any recent version should be good).

The code is written for x86_64 - other architectures are not supported but PRs are welcome to enable them.

Assuming you are running Ubuntu (or a variant) 24.04, these commands will
install the dependencies you need:
```bash
sudo apt update
sudo apt install build-essential bison flex libuv1-dev pkg-config git
```

You will also need telnet to connect to the server:
```bash
sudo apt install inetutils-telnet
```

## Build
Download and extract the latest release tarball from
https://github.com/archaro/sin/releases, then change into the extracted
directory.

Alternatively, clone the repository:
```bash
git clone https://github.com/archaro/sin.git
cd sin
```
Let the reader beware that the repository may be a moving target and isn't
guaranteed not to cause rains of frogs in Liechtenstein.  Caveat downloador.
Whichever option you choose, the next step is to build the executables:
```bash
make
```

This builds:

- `sin` (runtime engine)
- `scomp` (standalone compiler)
- `sdiss` (standalone disassembler)
- `sconv` (itemstore converter)

These executables are written to the repository root.  The tutorial assumes that
you are leaving them where they are.

## Run the chat server example

The chat server is a simple example of Sinistra in action, so this walkthrough
uses the chat source files consistently:

- `docs/guide/examples/chat-load.src` creates the persistent `input` code item that handles
  connections, disconnections, and incoming text.
- `docs/guide/examples/chat-boot.src` is the startup code that runs each time the server is
  launched.

From the repository root, compile each source file to bytecode object code:

```bash
./scomp docs/guide/examples/chat-load.src chat-load.obj
./scomp docs/guide/examples/chat-boot.src chat-boot.obj
```
This is what you should see (the compiler version and byte counts may differ
between releases):
```
archaro@stick:~/sin$ ./scomp docs/guide/examples/chat-load.src chat-load.obj
Sinistra compiler version 0.7.7
Source loaded: 2495 bytes from docs/guide/examples/chat-load.src.
Compiling...
Compilation completed: 2282 bytes.
Writing bytecode to chat-load.obj.
archaro@stick:~/sin$ ./scomp docs/guide/examples/chat-boot.src chat-boot.obj
Sinistra compiler version 0.7.7
Source loaded: 135 bytes from docs/guide/examples/chat-boot.src.
Compiling...
Compilation completed: 55 bytes.
Writing bytecode to chat-boot.obj.
archaro@stick:~/sin$ 
```

`./scomp <source> <object>` reads the Sinistra source file at `<source>` and
writes compiled bytecode to `<object>`. These object files will be processed by
`sin` - the first one will be run once to initialise the itemstore, while the
second is the bootstrap code, which will be run when the chat server starts up.

Initialize the itemstore once by running the load object in load-only mode:

```bash
./sin --loadonly -o chat-load.obj
```

You should see:
```
archaro@stick:~/sin$ ./sin --loadonly -o chat-load.obj
Bytecode loaded: 2282 bytes from chat-load.obj.
Creating new source root in current directory.
Using 'srcroot' as the source root.
Runtime options: loadonly=1 strict_validation=0 strict_runtime_contracts=0.
Creating a new itemstore, which will be saved as items.dat.
Shutting down.
archaro@stick:~/sin$
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

You should see:
```
archaro@stick:~/sin$ ./sin -o chat-boot.obj
Bytecode loaded: 55 bytes from chat-boot.obj.
Using 'srcroot' as the source root.
Runtime options: loadonly=0 strict_validation=0 strict_runtime_contracts=0.
Loading itemstore from items.dat.
Starting the chat server...
Using `input` as the input item.
Listening on port 4001.
Running...
```

Use `./sin -o ...` *without `--loadonly`* when you want the engine to start
normally and accept network connections.

## Connect to the server

Sinistra is now listening for network connections.  By default the listener
uses port `4001`.  To use a different port, pass `-p <port>` when starting the
server.

In another terminal, connect with telnet:
```bash
telnet localhost 4001
```

You should see:
```
archaro@stick:~/sin$ telnet localhost 4001
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
Connected.
Hello!  You are on line 0
```

In a third terminal window, run the same command to open another connection to
the server.  You will see a similar message, but this time you will be on
line 1.

Each connection receives notices when another line connects, sends text, or
disconnects.  You can now talk to yourself.  Type something into one connection,
hit enter, and see it appear on the other connection.

## Shut down safely

A safe shutdown is one that lets the engine leave the run loop normally and save
the in-memory itemstore back to `items.dat`. Sinistra code does that by calling
`sys.shutdown;`.

The chat server has two commands - both prefixed with `\`:
- \quit - logs you out
- \shutdown - shuts the server down

In the first telnet session, type `\quit`.  This will end the connection and
drop you back at the shell prompt.  In the second telnet session, type
`\shutdown`.  This will terminate Sinistra normally, persisting the itemstore
back to items.dat.

## Restart from saved state

After the one-time load-only run has created `items.dat`, you only need to
restart the server from saved state:

```bash
./sin -o chat-boot.obj
```

Do not rerun the `./sin --loadonly -o chat-load.obj` initialization
step unless you intentionally want to reload or replace the persistent `input`
item from `docs/guide/examples/chat-load.src`.
