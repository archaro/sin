# Sinistra - a deeply suspicious MUD engine

## What is it? ##
*Sinistra* comprises a compiler and a bytecode interpreter.  Source is compiled to a custom bytecode, which is then interpreted by the runtime engine.  The runtime engine is capable of calling the compiler directly, so this allows the system to be dynamically altered and expanded.  When the runtime engine starts, the data and *Sinistra* code is loaded into memory.  At the end of execution, the state is saved back to disk.  As to the style of the language: it is something like the misbegotten offspring of Forth and Smalltalk.

When the runtime engine starts up, it first loads and executes the bootstrap code (which is separately compiled).  The engine is event-driven and this code sets things up ready for the game to run, including setting up the main game tasks.  Tasks are attached to the runloop and are called as necessary.  There are three kinds:
- Network tasks: the listener, and any player connections created by it.  These tasks run outside the game and interact in limited ways with *Sinistra* code, and their purpose is to manage input from and output to connected players.
- Timer tasks: these are managed by *Sinistra* code (for example, the bootstrap code).  Each time the timer expires, the specified code is run.
- Input task: this is the most important task, and is run once per loop.  It processes connections, disconnections and data from the players and output back to them.  The input task expects to call the `input` item, which is written in *Sinistra*.  (And is, in fact, the only code item you *need* to write, making sure it calls the net.input libcall.)

## Limitations ##
- The structure of the language is broadly complete, but library functions are almost nonexistent.  Player connection/disconnection/activity is handled, but what can be done with such events is limited.  Still, the engine is sufficiently developed that it can run a simple echoserver.
- The code is written in the vernacular style without any formal design.  In other words, I'm making it up as I go along.  There are various code styles and design patterns in use, depending on the mood I was in when I wrote each particular bit of code.  Much of the code is quite naïve, and will require tightening up and optimisation.  I follow the mantra "first, make it work; then make it work well; finally make it work fast".
- The parser is singularly unhelpful if it doesn't like what it is compiling.
- Documentation?  What documentation?

## Building and Dependencies ##
The development environment is Ubuntu 24.04.4, but any modern Linux distro should be fine, as long as libuv1 is available (version 1.48 definitely works, but any recent version should be good).  The code is written for x86_64 - other architectures are not supported.  Just run make in the top-level directory, and the binaries will be built (`sin`, the runtime engine; `scomp`, the standalone compiler; `sdiss`, the standalone disassembler).  You can install the binaries wherever you want.  The runtime engine assumes everything happens in the current working directory, but you can use command-line options to change various defaults.

