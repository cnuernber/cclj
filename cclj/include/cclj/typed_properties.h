//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_TYPED_PROPERTIES_H
#define CCLJ_TYPED_PROPERTIES_H
#include "cclj/class_system_predefined_types.h"
#include "cclj/class_system.h"
#include "cclj/garbage_collector.h"

//template magic to make accessing properties as easy as possible.
namespace cclj
{
	template<typename data_type>
	struct type_to_name_map
	{
	};

#define CLLJ_DEFINE_TYPE_TO_NAME_MAP( data_type, type_name ) \
	template<> struct type_to_name_map<data_type>{ static const char* name() { return type_name; } };

	
#define CLLJ_DEFINE_TYPE_TO_NAME_MAP_default( data_type) \
	CLLJ_DEFINE_TYPE_TO_NAME_MAP( data_type, #data_type )

#define HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE( data_type ) \
	CLLJ_DEFINE_TYPE_TO_NAME_MAP_default( data_type)

	ITERATE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPES

#undef HANDLE_CCLJ_CLASS_SYSTEM_PREDEFINED_TYPE
		

	template<typename data_type>
	struct typed_property_entry
	{
		property_entry entry;
		typed_property_entry( const property_entry& ent )
			: entry( ent )
		{
		}

		data_type* data_ptr( gc_obj_ptr& obj, uint32_t index = 0 )
		{
			uint8_t* obj_data = reinterpret_cast<uint8_t*>( obj.data().first );
			data_type* dtype_ptr = reinterpret_cast<data_type*>( obj_data + entry.offset + index * entry.item_size );
			return dtype_ptr;
		}

		//common case of setting the first value.
		void set( gc_obj_ptr& obj, const data_type& type, uint32_t index = 0 )
		{
			*(data_ptr(obj, index)) = type;
		}

		data_type& get( gc_obj_ptr& obj, uint32_t index = 0 )
		{
			return *(data_ptr(obj, index));
		}
	};

	template<typename data_type>
	struct typed_property_definition
	{
		typedef data_type data_type;
		typedef typed_property_entry<data_type> property_type;

		const char* name;
		uint32_t	count;
		typed_property_definition( const char* _name, uint32_t _count = 1 )
			: name( _name )
			, count( _count )
		{
		}

		property_definition def( string_table_ptr str_table ) const
		{
			return property_definition( str_table->register_str( name )
				, str_table->register_str( type_to_name_map<data_type>::name() )
				, count );
		}

		static property_type to_entry( const property_entry& entry )
		{
			return property_type( entry );
		}
	};
	
	template<>
	struct typed_property_definition<objref_t>
	{
		typedef objref_t data_type;
		typedef typed_property_entry<gc_object*> property_type;

		const char* name;
		uint32_t	count;
		typed_property_definition( const char* _name, uint32_t _count = 1 )
			: name( _name )
			, count( _count )
		{
		}

		property_definition def( string_table_ptr str_table ) const
		{
			return property_definition( str_table->register_str( name )
				, str_table->register_str( type_to_name_map<data_type>::name() )
				, count );
		}

		static property_type to_entry( const property_entry& entry )
		{
			return property_type( entry );
		}
	};

}

#endif