# Quickstart

## Build

```bash
make
```

This builds:
- `sin` (runtime engine)
- `scomp` (standalone compiler)
- `sdiss` (standalone disassembler)

## Run the (very basic) chat server example

From the repository root:

```bash
./scomp examples/chat-boot.src examples/chat-boot.obj
./scomp examples/chat-load.src examples/chat-load.obj
./sin -b -o examples/echo-load.obj
./sin -o examples/echo-boot.obj
```
This will create `items.dat` and `srcroot\` in the current directory.  These make up the itemstore - data and code - for the instance.  To restart

Then connect with telnet to `localhost:4001`.

Kill the server by hitting ctrl-C in the shell where the engine is running.  If you wish to start it again, you only need to execute the startup command `./sin -o examples/echo-boot.obj` and this will reload its state from items.dat.
