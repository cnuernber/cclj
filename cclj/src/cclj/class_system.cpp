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
		vector<parent_class_entry>		_parents;
		vector<class_vtable_ptr>		_vtables;
		vector<class_function_ptr>		_instance_functions;
		const uint32_t					_instance_size;
		const uint8_t					_instance_alignment;
		vector<property_entry>			_all_properties_list;
		class_function_ptr				_destructor;

		class_definition_impl& operator=( const class_definition_impl& other );

		class_definition_impl( string_table_str nm
								, property_entry_const_buffer properties
								, parent_class_entry_const_buffer parents
								, uint32_t size
								, uint8_t alignment )
			: _name( nm )
			, _instance_size( size )
			, _instance_alignment( alignment )
		{
			_instance_properties.insert( _instance_properties.end(), properties.begin(), properties.end() );
			_parents.insert( _parents.end(), parents.begin(), parents.end() );
		}
		class_definition_impl( string_table_str nm
								, uint32_t size
								, uint8_t alignment )
								: _name( nm )
								, _instance_size( size )
								, _instance_alignment( alignment )
		{
		}

		virtual string_table_str name() const { return _name; }
		//properties do not include parent class properties.
		virtual property_entry_const_buffer instance_properties() const
		{
			return _instance_properties;
		}

		//Return the size and alignment of the instance.
		virtual uint32_t instance_size() { return _instance_size; }
		virtual uint8_t  instance_alignment() { return _instance_alignment; }

		virtual parent_class_entry_const_buffer parent_classes() const 
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


		property_entry_const_buffer all_properties(  )
		{
			if ( _all_properties_list.empty() )
			{
				auto parents = parent_classes();
				for_each( parents.begin(), parents.end(), 
					[this]( parent_class_entry parent_entry )
				{
					auto parent_props = parent_entry.definition->all_properties();
					uint32_t parent_offset = parent_entry.start_offset;
					transform( parent_props.begin(), parent_props.end()
									, inserter( _all_properties_list, _all_properties_list.end() )
									, [=]( const property_entry& prop_entry )
									{
										return property_entry( prop_entry.definition
																	, prop_entry.length
																	, prop_entry.offset + parent_offset );
									} );
				} );
				auto instance_props = instance_properties();
				_all_properties_list.insert( _all_properties_list.end()
												, instance_props.begin()
												, instance_props.end() );
			}

			return _all_properties_list;
		}

		virtual void set_destructor( class_function_ptr destructor ) { _destructor = destructor; }
		virtual class_function_ptr destructor() { return _destructor; }
	};

	struct class_system_impl : public class_system
	{
		vector<class_definition_ptr>							_definition_list;
		unordered_map<string_table_str,class_definition_ptr>	_definition_map;
		string_table_ptr										_string_table;

		template<typename data_type>
		void create_predefined_type( const char* _name )
		{
			string_table_str name = _string_table->register_str( _name );
			
			class_definition_ptr cls 
				= make_shared<class_definition_impl>( name, sizeof(data_type), sizeof( data_type ) );
			_definition_list.push_back( cls );
			_definition_map.insert( make_pair( name, cls ) );
		}

		class_system_impl( string_table_ptr table ) : _string_table( table ) 
		{
			//Create the predefined types.
#define HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE(tname)	\
			create_predefined_type<tname>( #tname );
			ITERATE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPES
#undef HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE

		}

		virtual string_table_ptr string_table() const { return _string_table; }
		
		virtual class_definition_ptr_const_buffer definitions() const
		{
			return _definition_list;
		}

		//no default because it couldn't be efficient enough with high volume of class definitions.
		virtual class_definition_ptr find_definition( string_table_str str ) const
		{
			auto iter = _definition_map.find( str );
			if ( iter != _definition_map.end() ) return iter->second;
			return class_definition_ptr();
		}

		virtual class_definition_ptr create_definition( string_table_str name
														, class_definition_ptr_const_buffer parents
														, property_definition_const_buffer properties
														, uint8_t minimum_alignment )
		{
			class_definition_ptr retval = find_definition(name);
			if ( retval ) return retval;
			uint32_t current_instance_offset = 0;
			uint8_t minimum_instance_alignment = std::max( (uint8_t)4, minimum_alignment );
			vector<parent_class_entry> parent_entries;

			//calculate where the instance properties can start
			for_each( parents.begin(), parents.end(), 
				[&](class_definition_ptr parent)
			{
				uint32_t parent_size = parent->instance_size();
				uint32_t parent_start_offset = align_number( current_instance_offset, parent->instance_alignment() );
				parent_entries.push_back( parent_class_entry( parent, parent_size ) );
				current_instance_offset = parent_start_offset + parent_size;
				minimum_instance_alignment = std::max( minimum_instance_alignment, parent->instance_alignment() );
			} );

			vector<property_entry> property_entries;
			for_each( properties.begin(), properties.end(),
				[&](const property_definition& def )
			{
				class_definition_ptr prop_type = find_definition( def.type );
				if ( !prop_type ) throw runtime_error( "failed to find property type class" );
				uint32_t num_items = std::max( (uint32_t)1, def.count );
				uint8_t prop_alignment = prop_type->instance_alignment();
				uint32_t prop_item_size = std::max( prop_type->instance_size(), (uint32_t)prop_alignment );
				uint32_t prop_size = prop_item_size * num_items;
				minimum_instance_alignment = std::max( minimum_instance_alignment, prop_alignment );
				current_instance_offset = align_number( current_instance_offset, prop_alignment );
				property_entry new_entry( def, current_instance_offset, prop_size );
				property_entries.push_back( new_entry );
			} );
			current_instance_offset = align_number( current_instance_offset, minimum_instance_alignment );
			if ( current_instance_offset == 0 )
				current_instance_offset = minimum_instance_alignment;

			class_definition_ptr cls 
				= make_shared<class_definition_impl>( name, property_entries, parent_entries
													, current_instance_offset
													, minimum_instance_alignment );
			_definition_list.push_back( cls );
			_definition_map.insert( make_pair( name, cls ) );
			return cls;
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
						, [=]( const parent_class_entry& classDef ) 
						{ 
							auto retval = classDef.definition->find_instance_property_recurse( name );
							if ( retval )
								retval->offset += classDef.start_offset;
							return retval;
						}
						, find_instance_property( name ) );
}
		
class_vtable_ptr class_definition::find_vtable( string_table_str name )
{
	return find_or_default( vtables(), [=]( class_vtable_ptr table ) { return table->name() == name; } );
}



class_vtable_ptr_and_offset class_definition::find_vtable_recurse( string_table_str name )
{
	return find_first_transform( parent_classes()
						, [=]( parent_class_entry classDef ) 
						{ 
							class_vtable_ptr_and_offset retval = classDef.definition->find_vtable_recurse( name );
							if ( retval )
								retval.second += classDef.start_offset;
							return retval; 
						}
						, class_vtable_ptr_and_offset( find_vtable( name ), 0 ) );
}

class_function_ptr class_definition::find_instance_function( string_table_str name )
{
	return find_or_default( instance_functions(), [=]( class_function_ptr fn ) { return fn->name() == name; } );
}

class_function_and_offset class_definition::find_instance_function_recurse( string_table_str name )
{
	return find_first_transform( parent_classes()
						, [=]( parent_class_entry classDef ) 
							{
								class_function_and_offset retval = 
										classDef.definition->find_instance_function_recurse( name );
								if ( retval )
									retval.second += classDef.start_offset;
								return retval;
							}
						, class_function_and_offset( find_instance_function( name ), 0 ) );
}

class_system_ptr class_system::create( string_table_ptr str_table )
{
	return make_shared<class_system_impl>( str_table );
}
