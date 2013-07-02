cclj
====

cclj is an experimental project to write a dynamic, typed language that compiles to binary and integrates quite closely with C++.  


It will use a LISP-like syntax but it will be strongly typed.  Typing adds a lot of noise to the program but it also allows some interesting features
such as type classes and such.  The type system will be as simple as possible and strong; no implicit type conversions.  


I wanted it to be optionally typed and that may follow but at first, in order to make the development simpler it will just be strongly, simply typed.


I do want meta programming facilities but not traditional lisp meta programming in that I want closer interaction with the compiler.  I would like the
meta programming system to be able to extend the compiler and to query information about program and type information.  As such, it will have to wait
until the main compiler is working to some extent.  For instance, it would be really good to be able to query the properties and functions on the
objects at compile time and generate code.  Or to query the enumeration values in an enumeration at compile to to generate string->enum mappings and
so forth.  


As far as the c++ integration goes, I would like to be able to make C++ virtual calls natively from the language.  This may require the clang AST to be
integrated to some extent.  I would also like to be able to call C functions and C++ member functions (non virtual).  Furthermore I would like
generalized support for the RAII paradigm; basically I would like to at any point be able to tell the compiler to ensure a function gets called before
the stack unwinds.  But I would like this to work at the closure/function level as opposed to the object level.  I would
also like support for c++ exceptions and stack unwinding although I do not know how difficult this is and it may be quite hard to do across compilers.


I like clojure's rules about syntax *especially* the differentiation using vectors and lists for different sections.  In general I do not want to get
into problems with precedence and I do not want to get into problems relating to function overloading (that is what type classes are for).  So the
language will be bare and simple.  I *do*, however, think namespaces are necessary.


I think clojure made a fairly large mistake in their dot notation system for member functions.  I understand the theory that the function is the first
member of the list, but the problem is that this makes intellisense far, far harder to achieve.  Great intellisense makes a large difference and,
like it or not, objects are huge part of the way people think and solve problems.


Gradual or optional typing seems very important to me.  It would be awesome if the types could be elided and there was a smooth path
to a normal dynamically typed list.  For instance:

    (defn add [a b] (+ a b)) ;works on objects, numbers are boxed and upcasted to number tower numbers where necessary.
	
	(defn add|f32 [a|f32 b|f32] ;works on floating point numbers only, compile or runtime error if miscalled
		(+ a b))
		
	(defn [num-type] add|num-type [a|num-type b|num-type] (+ a b)) ;works on anything where the + operator works


	
Should the type go in front or behind the variable?  A lot of inspiration comes from typed-script; the problem is colon notation.

	
Some possible examples:

	(namespace a
		
		(defn f32:fn [ f32:a i32:b i32:c ]
		  "some function information"
		  (let [d (+ b c)]
			(if (< d 4)
			  (a)
			  (a*a))))
	)
	
	(let [scoped_var (scope (fn [] (fopen fname)) (fn [:this] (fclose this))]
		  (fread (scoped_var.this)))
		  
		  
	
		  
		  
	; does lack of typing information make the language dynamic using objects
	; or does it mean the function is generic?  Probably dynamic as that would
	; make it match traditional LISP far closer.
		
		
		  
	