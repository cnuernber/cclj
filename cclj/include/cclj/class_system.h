//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_CLASS_SYSTEM_H
#define CCLJ_CLASS_SYSTEM_H
#include "cclj.h"
#include "cclj/string_table.h"
#include "cclj/data_buffer.h"
#include "cclj/option.h"

namespace cclj
{
	//At its core, a class system is just an inheritance based way of distributing properties.
	//A property is of fixed size in memory, so either a fixed size array, a defined item or an object
	//reference
	//However, we also attach groups of functions (vtables) and individual functions (instance functions)
	//to the object at runtime.  This allows some level of runtime extensibility.
	class property_definition
	{
	public:
		string_table_str	name;
		string_table_str	type;
		uint32_t			count;
		property_definition( string_table_str nm  = string_table_str()
								, string_table_str t = string_table_str()
								, uint32_t count = 1 )
			: name( nm )
			, type( t )
			, count( count )
		{
		}
	};

	typedef data_buffer<property_definition> property_definition_buffer;
	typedef const_data_buffer<property_definition> property_definition_const_buffer;

	class property_entry
	{
	public:
		property_definition definition;
		uint32_t			offset;
		uint32_t			length;

		property_entry( property_definition def = property_definition()
						, uint32_t of = 0, uint32_t len = 0 )
						: definition( def )
						, offset( of )
						, length( len )
		{
		}
	};

	typedef data_buffer<property_entry> property_entry_buffer;
	typedef const_data_buffer<property_entry> property_entry_const_buffer;
	typedef option<property_entry> property_entry_opt;


	//These are actually user defined by en large.  The class system only needs specific 
	//functions, 
	class class_function
	{
	protected:
		virtual ~class_function(){}
	public:
		friend class shared_ptr<class_function>;
		virtual string_table_str name() const = 0;
		//rtti enabler.
		virtual string_table_str type() const = 0;
	};

	typedef shared_ptr<class_function> class_function_ptr;
	typedef data_buffer<class_function_ptr> class_function_ptr_buffer;
	typedef const_data_buffer<class_function_ptr> class_function_ptr_const_buffer;

	class class_vtable
	{
	protected:
		virtual ~class_vtable(){}
	public:
		virtual string_table_str name() const = 0;
		virtual class_function_ptr_const_buffer functions() const = 0;
		//default implementation provided using std::find
		virtual class_function_ptr find_function( string_table_str name );
	};

	typedef shared_ptr<class_vtable> class_vtable_ptr;
	typedef data_buffer<class_vtable_ptr> class_vtable_ptr_buffer;
	typedef const_data_buffer<class_vtable_ptr> class_vtable_ptr_const_buffer;
	

	class class_definition;
	typedef shared_ptr<class_definition> class_definition_ptr;
	typedef data_buffer<class_definition_ptr> class_definition_ptr_buffer;
	typedef const_data_buffer<class_definition_ptr> class_definition_ptr_const_buffer;

	class parent_class_entry
	{
	public:
		class_definition_ptr	definition;
		uint32_t				start_offset;
		parent_class_entry( class_definition_ptr def, uint32_t off )
			: definition( def )
			, start_offset( off )
		{
		}
	};

	typedef const_data_buffer<parent_class_entry> parent_class_entry_const_buffer;

	struct class_function_and_offset : public pair<class_function_ptr,uint32_t>
	{
		class_function_and_offset( class_function_ptr fn = class_function_ptr(), uint32_t of = 0 )
			: pair<class_function_ptr,uint32_t>( fn, of )
		{
		}
		operator bool () const { return first; }
	};

	struct class_vtable_ptr_and_offset : public pair<class_vtable_ptr, uint32_t>
	{
		class_vtable_ptr_and_offset( class_vtable_ptr fn = class_vtable_ptr(), uint32_t of = 0 )
			: pair<class_vtable_ptr, uint32_t>( fn, of )
		{
		}
		operator bool () const { return first; }
	};

	class class_definition
	{
	protected:
		virtual ~class_definition(){}
	public:
		friend class shared_ptr<class_definition>;

		virtual string_table_str name() const = 0;
		//properties do not include parent class properties.
		virtual property_entry_const_buffer instance_properties() const = 0;
		//find property by name on this class only.  default implementation provided
		//using linear search with std::find.
		virtual property_entry_opt find_instance_property( string_table_str name );

		//default implementation searches this object, then all the parents recursively.
		//Returns a property with the correct offset.
		virtual property_entry_opt find_instance_property_recurse( string_table_str name );

		//Return the size and alignment of the instance.
		virtual uint32_t instance_size() = 0;
		//minimum alignment and thus minimum size is 4 bytes.
		virtual uint32_t instance_alignment() = 0;
		virtual parent_class_entry_const_buffer parent_classes() const = 0;
		virtual class_vtable_ptr_const_buffer vtables() const = 0;
		//You cannot change the data layout of a class after it has been registered.  You can, however,
		//add vtables to the class.
		virtual void register_vtable( string_table_str name, class_function_ptr_const_buffer functions ) = 0;
		virtual class_vtable_ptr find_vtable( string_table_str name );
		//find vtable, if not exist on this object find it on parents.
		virtual class_vtable_ptr_and_offset find_vtable_recurse( string_table_str name );
		
		//Likewise to vtables, instance functions are extensible at runtime.
		virtual class_function_ptr_const_buffer instance_functions() const = 0;
		virtual void set_instance_function( class_function_ptr fn ) = 0;

		//default implementation provided use std::find.
		virtual class_function_ptr find_instance_function( string_table_str name );
		virtual class_function_and_offset find_instance_function_recurse( string_table_str name );
		//All properties including parents.
		virtual property_entry_const_buffer all_properties() = 0;
	};



	class class_system
	{
	protected:
		virtual ~class_system(){}
	public:
		friend class shared_ptr<class_system>;

		virtual string_table_ptr string_table() const = 0;

		virtual class_definition_ptr_const_buffer definitions() const = 0;

		//no default because it couldn't be efficient enough with high volume of class definitions.
		virtual class_definition_ptr find_definition( string_table_str str ) const = 0;
		virtual class_definition_ptr create_definition( string_table_str name
														, class_definition_ptr_const_buffer parents
														, property_definition_const_buffer properties ) = 0;

		static shared_ptr<class_system> create(string_table_ptr str_table );
	};



	typedef shared_ptr<class_system> class_system_ptr;
}

#endif