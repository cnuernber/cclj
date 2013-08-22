# Decomposing Object Oriented Language Features

This document is an attempt to decompose object oriented systems into pieces that can be implemented and be somewhat
orthogonal.  Specifically I would like to separate out objects from scoping from simple datatypes, etc.


For this discussion:
* A datastructure is a C struct type.  
* An object is the thing we are trying to define.
* Member functions are analogous to non-virtual member functions.
* Runtime-polymorphic functions are virtual functions.


Compile time polymorphism is handled completely by either specializing templates or by macros so it will not be
discussed although compile time polymorphic functions can be member functions (they cannot be both compile time
polymorphic and runtime polymorphic.


Member functions seem to be nothing more than intellisense help along with some syntax sugar.  The intellisense help is
extremely helpful so I wouldn't get rid of it but it isn't clear to me why member functions need to be all declared
inside a given type description instead of declared wherever needed.
[Here](http://msdn.microsoft.com/en-us/library/vstudio/bb383977.aspx) is the inspiration for this.  Allowing types to be
extended ad-hoc allows specialization of existing types without requiring creating new types.


Interfaces (collections of either runtime-polymorphic or member functions) are used in at least two forms.  The first
form simply guarantees that a set of functions will be found for that type but elides the datastructures needed to
implement the type.  This type of programming isn't well supported in c++ because it forces pure virtual objects or impl
designs but it is quite often used regardless.


The second form declares a set of runtime-polymorphic functions that are found with the type, allowing the traditional
runtime polymorphism to be implemented efficiently if you can dispatch off the first argument to the function.  What
this means is that the first argument contains a hidden pointer to a dispatch table (the vtable for that interface) but
the rest of the arguments are simply used to find the function in the dispatch table.  Multiple argument dispatch is a
compile time only feature of C++ although systems like clojure allow it at runtime, they do so via providing a dispatch
function that tests either values or types or both.


For C++ type objects, the compiler attempts to ensure some object invariants.  These are useful but they also are a
source of C programmer's consternation with C++.  The compiler simply isn't smart enough to handle
construction/destruction of bulk sets of objects efficiently meaning sometimes you get unreasonable performance from
seemingly trivial-to-optimize code.


Speaking of which there are a rather large set of objects where simple memory operations are appropriate (memset,
memcpy) to implement most of the invariants.  They don't need destructors and they consist only of datastructures with
trivial constructors.


In general I think object invariants are necessary for large classes of objects but importantly not for all objects.


For the compiler to construct a type, it needs a full record of all datastructures used in the type.  If the type
includes runtime-polymorphic functions, then it needs the full record of all of the runtime-polymorphic functions.


It has come out in recent years that allowing the addition of interfaces (collections of methods, runtime-polymorphic or
otherwise) to a given type is useful for certain problem sets.


The set of interfaces found with the type under all conditions are needed at type definition time but if provisions are
made for extension interfaces then these interfaces do not need to be declared at type definition type.  If the type
had not only one vtable but a linked list of vtables then it would be reasonable to allow the type to be extended ad-hoc
via unrelated files.



Items required at type-definition time
 * datastructures used in the type
 * Initial interface the type implements if any
 * at least one constructor function
 * exactly one destructor function

Items optional at type-definition time
 * copy constructor
 * equality operator


There is no reason to require the type's entire declaration to be in a single file.
