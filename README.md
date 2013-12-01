cclj
====

cclj is an experimental project to write a dynamic, typed language that compiles to binary and integrates quite closely with C++.  

cclj builds on windows using Visual Studio 13 or Visual Studio 13 Express.  The project also builds on a linux platform.

On all supported operating systems:
Install perl.  From cpan (or the cygwin package system) install Data::UUID and XML::libXML.  Install python 2.7.X series.  Also install cmake.
If you are under windows, you need the cmake for windows not cygwin cmake.  cygwin cmake doesn't include visual studio support.

Get llvm 3.4 src code, place in a sibling directory to cclj named "llvm-3.4.src".
Create another directory sibling to cclj named "llvmbuild".

Build llvm on windows:
----------------------
under llvmbuild:

1. cmake -G "Visual Studio 12" ..\llvm-3.4.src
2. Open solution and build both release and debug.


Build llvm on linux:
-----------------------
under llvmbuild:

1. cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ../llvm-3.4.src -j4
2. make.
3. mkdir lib/Release
4. mkdie lib/Debug
5. mv lib/* lib/Release
6. mkdir ../llvmbuild2
7. cd ../llvmbuild2
8. make -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug ../llvm-3.4.src -j4
9. mv lib/* ../llvmbuild/lib/Debug


Build cclj
------------------------
1. cd build/xpj
2. perl xpjp.pl

On windows, your build files will be located under ../Win32
On linux, your build files will be located under ../linux.



Language examples
=====================

* All of the basic files under corpus/basic*.cclj are good starting points.
* [C integration, pointers](corpus/dynamic_mem.cclj)
* [compile time programming, simple](corpus/basic2.cclj)
* [compile time programming, advanced](corpus/macro_fn2.cclj)
* [C++ style template functions](corpus/poly_fn.cclj)
* [scope aware programming (RAII)](corpus/scope_exit.cclj)




Compiler Architecture
========================
This needs to be fleshed out more in the wiki but here are the main points.

* Processing of the lisp code, once passed the reader, is highly modular.
* The lisp preprocessing system can be extended with lisp_evaluators.
* Most features are implemented with compiler plugins.  In fact the entire language is implemented
with compiler plugins

A compiler plugin is responsible for taking a lisp expression and translating it into an AST node.
An AST node is mainly responsible for producing llvm code (although they can also be responsible for
calling functions and performing variable resolution).

At this point it may or may not be clear that in addition to allowing lisp style standard compile time
programming, c++ style type-based template compile time programming, I intend to allow the intrepid to provide
their own compiler plugin shared libraries and thus produce a third type of compile time programming, one
that allows new special forms along with their associated translation to llvm assembly.  If statements, for loops,
everything that runs currently is all done with compiler plugins (including macros and the entire 
lisp preprocessing system).

The plugin interface is described [here](cclj/include/cclj/plugins/compiler_plugin.h).