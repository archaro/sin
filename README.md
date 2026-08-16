# Sinistra - a deeply suspicious MUD engine

## What is it? ##
*Sinistra* comprises a compiler and a bytecode interpreter.  Source is compiled to a custom bytecode, which is then interpreted by the runtime engine.  The runtime engine is capable of calling the compiler directly, so this allows the system to be dynamically altered and expanded.  When the runtime engine starts, the data and *Sinistra* code is loaded into memory.  At the end of execution, the state is saved back to disk.

## Limitations ##
- The structure of the language is broadly complete, but the library surface is still small.  Player connection/disconnection/activity is handled, and the current examples demonstrate simple echo and chat servers.
- The code is written in the vernacular style without any formal design.  In other words, I'm making it up as I go along.  There are various code styles and design patterns in use, depending on the mood I was in when I wrote each particular bit of code.  Much of the code is quite naïve, and will require tightening up and optimisation.  I follow the mantra "first, make it work; then make it work well; finally make it work fast".
- I use LLMs (mostly Codex) as a programming partner.  I generally work by providing an extremely detailed technical overview of the current task, and then work iteratively with the LLM until the code does what I want it to do.  I have found this approach to be broadly satisfactory, although it isn't everyone's drinking receptacle of a hot beverage.
- The compiler isn't particularly helpful if it encounters something it doesn't like, but it is improving.
- Documentation is maintained under [`docs/`](docs/README.md). The implementation-derived references are current for the pre-release runtime;
  [`docs/documentation-roadmap.md`](docs/documentation-roadmap.md) lists the remaining reader-guide and language-reference work.
- The language freeze should happen with pre-release 0.8.0.

## Compiling and Running ##

See [`QUICKSTART.md`](docs/guide/quickstart.md) for instructions on compiling some code and starting the runtime engine.
Development prerequisites and the local/hosted CI gates are documented in [`CONTRIBUTING.md`](CONTRIBUTING.md).
