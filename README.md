# Sinistra - a deeply suspicious MUD system

## History ##
This project was inspired by *SLIP*, a language created in the early 1980s by Lorenzo Wood and Nat Billington, for the game *Chaos - World of Wizards*.  This game ran on *IOWA (Input/Output World of Adventure)* a multi-user BBS owned and operated by Pip Cordrey in the UK.  *SLIP* was one of the first languages I encountered after BASIC, and I have retained fond memories of it ever since.  This current project was borne out of the discovery of half of a *SLIP* programmers' reference, in the bottom of a packing crate during one of my many house-moves.  The original language was remarkably powerful for something which ran on an Acorn Archimedes (early RISC machine), and it deserves not to be forgotten.

## So what is it? ##
Essentially, *Sinistra* comprises a compiler and a bytecode interpreter.  Source is compiled to a custom bytecode, which is then interpreted by the runtime engine.  The runtime engine is capable of calling the compiler directly, so this allows the system to be dynamically altered and expanded.  When the runtime engine starts, the data and *Sinistra* code is loaded into memory.  At the end of execution, the state is saved back to disk.  As to the style of the language: it is something like the misbegotten offspring of Forth and Smalltalk.

When the runtime engine starts up, it first loads and executes the bootstrap code (which is separately compiled).  The engine is event-driven and this code sets things up ready for the game to run, including setting up the main game tasks.  Tasks are attached to the runloop and are called as necessary.  There are three kinds:
- Network tasks: the listener, and any player connections created by it.  These tasks run outside the game and interact in limited ways with *Sinistra* code, and their purpose is to manage input from and output to connected players.
- Timer tasks: these are managed by *Sinistra* code (for example, the bootstrap code).  Each time the timer expires, the specified code is run.
- Input task: this is the most important task, and is run once per loop.  It processes connections, disconnections and data from the players and output back to them.

## Limitations ##
- The structure of the language is broadly complete, but library functions are almost nonexistent.  In terms of I/O, for example, there is precisely one method of output (sys.log - outputs to the system log) and, while client connections and disconnections are handled, data received is not yet processed (it just sits there in the connection buffer).  This is the current development focus, because otherwise it's just a potato.
- The code is written in the vernacular style without any formal design.  In other words, I'm making it up as I go along.  There are various code styles and design patterns in use, depending on the mood I was in when I wrote each particular bit of code.  Much of the code is quite naïve, and will require tightening up and optimisation.  I follow the mantra "first, make it work; then make it work well; finally make it work fast".
- The parser is singularly unhelpful if it doesn't like what it is compiling.
- Documentation?  What documentation?

## Building and Dependencies ##
The development environment is Ubuntu 22.04, but any modern Linux distro should be fine, as long as libuv1 is available.  The code is written for x86_64 - other architectures are not supported.  Just run make in the top-level directory, and the binaries will be built (`sin`, the runtime engine; `scomp`, the standalone compiler; `sdiss`, the standalone disassembler).  You can install the binaries wherever you want.  The runtime engine assumes everything happens in the current working directory, but you can use command-line options to tell it otherwise.

## Contributions ##
Are welcomed.  In fact, they are positively encouraged.  Send me a pull request.  *This project uses the MIT License*.

