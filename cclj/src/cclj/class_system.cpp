//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/class_system.h"
#include "cclj/class_system_predefined_types.h"
#include "cclj/algo_util.h"

using namespace cclj;

namespace {
	

	struct vtable_impl : public class_vtable
	{
		string_table_str				_name;
		vector<class_function_ptr>		_functions;

		vtable_impl( string_table_str nm, class_function_ptr_const_buffer fns )
			: _name( nm )
		{
			_functions.insert( _functions.end(), fns.begin(), fns.end() );
		}
		
		virtual string_table_str name() const { return _name; }
		virtual class_function_ptr_const_buffer functions() const  {return _functions;}
	};


	struct class_definition_impl : public class_definition
	{
		string_table_str				_name;
		vector<property_entry>			_instance_properties;
		vector<class_definition_ptr>	_parents;
		vector<class_vtable_ptr>		_vtables;
		vector<class_function_ptr>		_instance_functions;
		const uint32_t					_instance_size;
		const uint8_t					_instance_alignment;

		class_definition_impl& operator=( const class_definition_impl& other );

		class_definition_impl( string_table_str nm
								, property_entry_const_buffer properties
								, class_definition_ptr_const_buffer parents
								, uint32_t size
								, uint8_t alignment )
			: _name( nm )
			, _instance_size( size )
			, _instance_alignment( alignment )
		{
			_instance_properties.insert( _instance_properties.end(), properties.begin(), properties.end() );
			_parents.insert( _parents.end(), parents.begin(), parents.end() );
		}

		virtual string_table_str name() const { return _name; }
		//properties do not include parent class properties.
		virtual property_entry_const_buffer instance_properties() const
		{
			return _instance_properties;
		}

		//Return the size and alignment of the instance.
		virtual uint32_t instance_size() { return _instance_size; }
		virtual uint32_t instance_alignment() { return _instance_alignment; }

		virtual class_definition_ptr_const_buffer parent_classes() const 
		{
			return _parents;
		}

		virtual class_vtable_ptr_const_buffer vtables() const 
		{
			return _vtables;
		}

		//You cannot change the data layout of a class after it has been registered.  You can, however,
		//add vtables to the class.
		virtual void register_vtable( string_table_str name, class_function_ptr_const_buffer functions )
		{
			insert_or_override( _vtables
								, [=]( class_vtable_ptr table ) { return table->name() == name; }
								, make_shared<vtable_impl>( name, functions ) );
		}
		
		//Likewise to vtables, instance functions are extensible at runtime.
		virtual class_function_ptr_const_buffer instance_functions() const
		{
			return _instance_functions;
		}

		virtual void set_instance_function( class_function_ptr _fn )
		{
			insert_or_override( _instance_functions
								, [=] ( class_function_ptr fn ) { return _fn->name() == fn->name(); }
								, _fn );
		}
	};
}

class_function_ptr class_vtable::find_function( string_table_str nm )
{
	return find_or_default( functions(), [=]( class_function_ptr fn ) { return fn->name() == nm; } );
}


//find property by name on this class only.  default implementation provided
//using linear search with std::find.
property_entry_opt class_definition::find_instance_property( string_table_str name )
{
	return find_or_default( instance_properties()
						, [=](const property_entry& entry) { return name == entry.definition.name; } );
}

//default implementation searches this object, then all the parents recursively.
property_entry_opt class_definition::find_instance_property_recurse( string_table_str name )
{
	return find_first_transform( parent_classes()
						, [=]( class_definition_ptr classDef ) { return classDef->find_instance_property( name ); }
						, find_instance_property( name ) );
}
		
class_vtable_ptr class_definition::find_vtable( string_table_str name )
{
	return find_or_default( vtables(), [=]( class_vtable_ptr table ) { return table->name() == name; } );
}

class_vtable_ptr class_definition::find_vtable_recurse( string_table_str name )
{
	return find_first_transform( parent_classes()
						, [=]( class_definition_ptr classDef ) { return classDef->find_vtable( name ); }
						, find_vtable( name ) );
}
class_function_ptr class_definition::find_instance_function( string_table_str name )
{
	return find_or_default( instance_functions(), [=]( class_function_ptr fn ) { return fn->name() == name; } );
}

class_function_ptr class_definition::find_instance_function_recurse( string_table_str name )
{
	return find_first_transform( parent_classes()
						, [=]( class_definition_ptr classDef ) { return classDef->find_instance_function( name ); }
						, find_instance_function( name ) );
}
