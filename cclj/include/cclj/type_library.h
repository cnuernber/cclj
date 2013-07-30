//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_TYPE_LIBRARY_H
#define CCLJ_TYPE_LIBRARY_H
#pragma once
#include "cclj/cclj.h"
#include "cclj/data_buffer.h"
#include "cclj/string_table.h"
#include "cclj/allocator.h"

namespace cclj
{
	

#define CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES	\
	CCLJ_HANDLE_LIST_NUMERIC_TYPE( f32 )		\
	CCLJ_HANDLE_LIST_NUMERIC_TYPE( f64 )		\
	CCLJ_HANDLE_LIST_NUMERIC_TYPE( i1 )			\
	CCLJ_HANDLE_LIST_NUMERIC_TYPE( i8 )			\
	CCLJ_HANDLE_LIST_NUMERIC_TYPE( u8 )			\
	CCLJ_HANDLE_LIST_NUMERIC_TYPE( i16 )		\
	CCLJ_HANDLE_LIST_NUMERIC_TYPE( u16 )		\
	CCLJ_HANDLE_LIST_NUMERIC_TYPE( i32 )		\
	CCLJ_HANDLE_LIST_NUMERIC_TYPE( u32 )		\
	CCLJ_HANDLE_LIST_NUMERIC_TYPE( i64 )		\
	CCLJ_HANDLE_LIST_NUMERIC_TYPE( u64 )		

	struct base_numeric_types
	{
		enum _enum
		{
			no_known_type = 0,
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name ) name,
CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
		};
		static bool is_float_type( _enum val )
		{
			return val == f32 || val == f64;
		}

		static bool is_unsigned_int_type( _enum val )
		{
			return val == u8
				|| val == u16
				|| val == u32
				|| val == u64;
		}

		static bool is_signed_int_type( _enum val )
		{
			return val == i8
				|| val == i16
				|| val == i32
				|| val == i64;
		}

		static bool is_int_type( _enum val )
		{
			return is_unsigned_int_type( val )
				|| is_signed_int_type( val );
		}

		static uint8_t num_bits( _enum val )
		{
			switch( val )
			{
			case f32: return 32;
			case f64: return 64;
			case i1: return 1; //probably 8 but whatever.
			case i8: case u8: return 8;
			case i16: case u16: return 16;
			case i32: case u32: return 32;
			case i64: case u64: return 64;
			}
			throw runtime_error( "invalid value to count bits" );
		}
	};

	template<base_numeric_types::_enum>
	struct numeric_type_to_c_type_map
	{
	};
	template<> struct numeric_type_to_c_type_map<base_numeric_types::f32> { typedef float numeric_type; };
	template<> struct numeric_type_to_c_type_map<base_numeric_types::f64> { typedef double numeric_type; };
	template<> struct numeric_type_to_c_type_map<base_numeric_types::i1> { typedef bool numeric_type; };
	template<> struct numeric_type_to_c_type_map<base_numeric_types::i8> { typedef int8_t numeric_type; };
	template<> struct numeric_type_to_c_type_map<base_numeric_types::u8> { typedef uint8_t numeric_type; };
	template<> struct numeric_type_to_c_type_map<base_numeric_types::i16> { typedef int16_t numeric_type; };
	template<> struct numeric_type_to_c_type_map<base_numeric_types::u16> { typedef uint16_t numeric_type; };
	template<> struct numeric_type_to_c_type_map<base_numeric_types::i32> { typedef int32_t numeric_type; };
	template<> struct numeric_type_to_c_type_map<base_numeric_types::u32> { typedef uint32_t numeric_type; };
	template<> struct numeric_type_to_c_type_map<base_numeric_types::i64> { typedef int64_t numeric_type; };
	template<> struct numeric_type_to_c_type_map<base_numeric_types::u64> { typedef uint64_t numeric_type; };


	class type_ref;
	typedef type_ref* type_ref_ptr;
	typedef data_buffer<type_ref_ptr> type_ref_ptr_buffer;
	class type_ref
	{
	public:
		string_table_str			_name;
		type_ref_ptr_buffer			_specializations;
	};

	class type_library
	{
	protected:
		virtual ~type_library(){}
	public:
		friend class shared_ptr<type_library>;

		virtual string_table_ptr string_table() = 0;

		//type system ensures the type refs are pointer-comparable.
		virtual type_ref& get_type_ref( string_table_str name
									, type_ref_ptr_buffer _specializations = type_ref_ptr_buffer() ) = 0;

		type_ref& get_type_ref( const char* name, type_ref_ptr_buffer _specializations = type_ref_ptr_buffer() )
		{
			return get_type_ref( string_table()->register_str( name ), _specializations );
		}

		type_ref& get_ptr_type( type_ref& type )
		{
			type_ref* type_ptr( &type );
			type_ref_ptr_buffer specs( &type_ptr, 1 );
			return get_type_ref( "ptr", specs );
		}

		//void ptr
		type_ref& get_unqual_ptr_type()
		{
			return get_ptr_type( get_type_ref( "unqual" ) );
		}

		type_ref& get_ptr_type( base_numeric_types::_enum type )
		{
			type_ref* num_type = &get_type_ref( type );
			type_ref_ptr_buffer specs( &num_type, 1 );
			return get_type_ref( "ptr", specs );
		}

		type_ref& deref_ptr_type( type_ref& src_type )
		{
			if ( src_type._name == string_table()->register_str( "ptr" ) 
				&& src_type._specializations[0] )
				return *src_type._specializations[0];
			throw runtime_error( "invalid ptr deref" );
		}

		virtual type_ref& get_type_ref( base_numeric_types::_enum type )
		{
			switch( type )
			{
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name ) case base_numeric_types::name: return get_type_ref( #name );
CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
			}
			throw runtime_error( "unrecognized base numeric type" );
		}

		base_numeric_types::_enum to_base_numeric_type( type_ref& dtype )
		{
			if ( dtype._specializations.size() )
				return base_numeric_types::no_known_type;
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE(name) \
			if ( dtype._name == string_table()->register_str( #name ) ) \
				return base_numeric_types::name;
				CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
			return base_numeric_types::no_known_type;
		}

		static shared_ptr<type_library> create_type_library( allocator_ptr allocator, string_table_ptr str_table );
	};

	typedef shared_ptr<type_library> type_library_ptr;
	
}


#endif