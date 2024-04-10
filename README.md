# Sinistra - a deeply suspicious MUD system

## History ##
This project was inspired by *SLIP*, a language created in the early 1980s by Lorenzo Wood and Nat Billington, for the game *Chaos - World of Wizards*.  This game ran on *IOWA (Input/Output World of Adventure)* a multi-user BBS owned and operated by Pip Cordrey in the UK.  *SLIP* was one of the first languages I encountered after BASIC, and I have retained fond memories of it ever since.  This current project was borne out of the discovery of half of a *SLIP* programmers' reference, in the bottom of a packing crate during one of my many house-moves.  The original language was remarkably powerful for something which ran on an Acorn Archimedes (early RISC machine), and it deserves not to be forgotten.

## So what is it? ##

Essentially, *Sinistra* comprises a compiler and a bytecode interpreter.  Source is compiled to a custom bytecode, which is then interpreted by the runtime engine.  The runtime engine is capable of calling the compiler directly, so this allows the system to be dynamically altered and expanded.  When the runtime engine starts, the data and *Sinistra* code is loaded into memory.  At the end of execution, the state is saved back to disk.  As to the style of the language: it is something like the misbegotten offspring of Forth and Smalltalk.

Will it amount to anything?  Who knows?

This file was written solely to make Github shut up.
