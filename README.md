Clox
====

[![Build Status](https://travis-ci.com/luke-gru/clox.svg?branch=master)](https://travis-ci.com/luke-gru/clox)

Lox (clox) is an object-oriented scripting language developed alongside the reading of
craftinginterpreters (craftinginterpreters.com) by Bob Nystrom.

I've followed along, but added my own features as well.

Added features
--------------

* Multi-threading with libpthread and a global VM lock. Threads can run
  concurrently for IO and other blocking operations, but cannot run concurrently
  in the VM itself (like cpython and cruby).
* try/catch exception handling
* Splatted (rest) parameters and arguments
* Map (hash/dict) literals
* Regular expressions
* Default arguments
* Keyword arguments
* Mutable strings
* Modules similar to Ruby's
* Blocks similar to Ruby's
* Iterators and foreach() statement
* Integrated REPL
* String interpolation
* Multi-process support using fork() syscall
* Signal handling: registering signal handlers, sending signals
* Small standard library
* Object finalizers

Some internal differences with the book
---------------------------------------

* creation of AST before compilation phase (separate parser/compiler)
* bytecode optimization passes (including constant folding)
* Generational M&S garbage collector with managed heaps

Future features
---------------

* Standard library networking support
* Support string methods that work on utf8 codepoints
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

Here's an example from the standard library:

```
class Benchmark {
  class start(name: "Benchmark", iterations: 10) {
    var times = [];
    print "Running ${iterations} iterations";
    for (var i = 0; i < iterations; i+=1) {
      var t1 = Timer();
      yield();
      var t2 = Timer();
      times << (t2 - t1);
    }
    this.report(name, iterations, times);
  }

  class report(name, iters, times) {
    print "============";
    print name;
    print "============";
    print "Iterations: ${iters}";
    var sum = 0;
    for (var i = 0; i < times.size(); i+=1) {
      sum += times[i].seconds();
    }
    var avg = sum/times.size();
    print "Average iteration (s): ${avg}";
  }
}
```

See the examples folder for more examples, and the lib folder for the standard library.
The files in the examples folder are also tests run by the ./bin/test\_examples
binary (see Makefile for more details).

Thanks
------
Thanks to Bob Nystrom for the book and github repo!
