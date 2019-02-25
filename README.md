Clox
====

[![Build Status](https://travis-ci.com/luke-gru/clox.svg?branch=master)](https://travis-ci.com/luke-gru/clox)

Lox (clox) is an object-oriented scripting language developed alongside the reading of
craftinginterpreters (craftinginterpreters.com) by Bob Nystrom.

I've followed along, but added my own features as well.

Added features
--------------
* Splatted (rest) parameters and arguments
* Default arguments
* Keyword arguments
* Mutable strings
* Modules similar to Ruby's
* Blocks similar to Ruby's
* Integrated REPL
* Multi-process support using fork() syscall
* Basic multi-threading with libpthread and a global VM lock. Threads can run
  concurrently for IO and other blocking operations, but cannot run concurrently
  in the VM itself (like cpython and cruby).
* try/catch exception handling
* Iterators and foreach() statement
* String interpolation
* Map (hash/dict) literals
* Object finalizers
* Regular expressions
* Signal handling: registering signal handlers, sending signals
* Small standard library

Some internal differences with the book
---------------------------------------
* creation of AST before compilation phase (separate parser/compiler)
* bytecode optimization passes (including constant folding)
* Generational M&S garbage collector with managed heaps

Future features
---------------
* Standard library networking support
* Support non-ascii strings
* Add constants (no redefinitions, will given compiler or runtime error)
* See TODO for more info
* Add JIT compiler, either method or tracing

OS/compiler support
-------------------
* Only tested on linux (Ubuntu 16.04) and mac OS, uses fork() and libpthread, as well as some C99 features
* Tested with gcc and clang
* Almost C++ compliant (g++ compiles, clang++ doesn't)

Examples
--------
See examples folder. These are also tests run by the ./bin/test\_examples
binary (see Makefile for more details).

Thanks
------
Thanks to Bob Nystrom for the book and github repo!
