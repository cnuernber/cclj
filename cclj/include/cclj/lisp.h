//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_LISP_H
#define CCLJ_LISP_H
#pragma once

#include "cclj/cclj.h"
#include "cclj/string_table.h"
#include "cclj/data_buffer.h"
#include "cclj/noncopyable.h"
#include "cclj/allocator.h"

namespace cclj
{
	namespace lisp
	{

		struct types
		{
			enum _enum
			{
				unknown_type = 0,
				cons_cell,
				symbol,
				array,
				constant,
				type_ref,
			};
		};


		class object: public noncopyable
		{
		protected:
		public:
			virtual ~object(){}
			virtual types::_enum type() const = 0;
		};


		typedef object* object_ptr;
		typedef data_buffer<object_ptr> object_ptr_buffer;


		class cons_cell : public object
		{
		public:
			object* _value;
			object* _next;
			cons_cell()
				: _value( nullptr )
				, _next( nullptr )
			{
			}
			enum { item_type = types::cons_cell };
			virtual types::_enum type() const { return types::cons_cell; }
		};


		class array : public object
		{
		public:
			object_ptr_buffer _data;
			enum { item_type = types::array };
			virtual types::_enum type() const { return types::array; }
		};

		class type_ref;
		typedef type_ref* type_ref_ptr;
		typedef data_buffer<type_ref_ptr> type_ref_ptr_buffer;
		
		class type_ref : public object
		{
		public:
			string_table_str			_name;
			type_ref_ptr_buffer			_specializations;
			enum { item_type = types::type_ref };
			virtual types::_enum type() const { return types::type_ref; }
		};



		class symbol : public object
		{
		public:
			string_table_str	_name;
			type_ref*			_type;
			symbol() : _type( nullptr ) {}

			enum { item_type = types::symbol };
			virtual types::_enum type() const { return types::symbol; }
		};


		class constant : public object
		{
		public:
			type_ref*		_type;
			uint8_t*		_value; 
			constant() : _type( nullptr ), _value( nullptr ) {}

			enum { item_type = types::constant };
			virtual types::_enum type() const { return types::constant; }
		};

		class object_traits
		{
		public:
			template<typename obj_type>
			static obj_type* cast( object* obj )
			{
				if ( obj == nullptr ) return nullptr;
				if ( obj->type() == obj_type::item_type ) return static_cast<obj_type*>( obj );
				return nullptr;
			}

			template<typename obj_type>
			static obj_type& cast( object& obj )
			{
				if ( obj.type() == obj_type::item_type ) return static_cast<obj_type&>( obj );
				throw runtime_error( "invalid cast" );
			}

			template<typename obj_type>
			static obj_type& cast_ref( object* obj )
			{
				if ( obj->type() == obj_type::item_type ) return *static_cast<obj_type*>( obj );
				throw runtime_error( "invalid cast" );
			}
		};

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


		class type_system
		{
		protected:
			virtual ~type_system(){}
		public:
			friend class shared_ptr<type_system>;

			virtual string_table_ptr string_table() = 0;

			//type system ensures the type refs are pointer-comparable.
			virtual type_ref& get_type_ref( string_table_str name
										, type_ref_ptr_buffer _specializations = type_ref_ptr_buffer() ) = 0;

			type_ref& get_type_ref( const char* name, type_ref_ptr_buffer _specializations = type_ref_ptr_buffer() )
			{
				return get_type_ref( string_table()->register_str( name ), _specializations );
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



			static shared_ptr<type_system> create_type_system( allocator_ptr allocator, string_table_ptr str_table );
		};

		typedef shared_ptr<type_system> type_system_ptr;


		class factory
		{
		protected:
			virtual ~factory(){}
		public:
			friend class shared_ptr<factory>;
			virtual const cons_cell& empty_cell() = 0;
			virtual cons_cell* create_cell() = 0;
			virtual array* create_array() = 0;
			virtual symbol* create_symbol() = 0;
			virtual constant* create_constant() = 0;
			virtual uint8_t* allocate_data( size_t size, uint8_t alignment ) = 0;
			virtual object_ptr_buffer allocate_obj_buffer(size_t size) = 0;

			static shared_ptr<factory> create_factory( allocator_ptr allocator, const cons_cell& empty_cell );
		};

		typedef shared_ptr<factory> factory_ptr;

		class reader
		{
		public:
			
			friend class shared_ptr<reader>;
			//Probably need some erro reporting in there somewhere.
			virtual object_ptr_buffer parse( const string& str ) = 0;

			static shared_ptr<reader> create_reader( factory_ptr factory
														, type_system_ptr type_system
														, string_table_ptr str_table );
		};

		typedef shared_ptr<reader> reader_ptr;


		
		static vector<string> split_symbol( symbol& sym )
		{
			vector<string> retval;
			string temp(sym._name.c_str());
			size_t last_offset = 0;
			for ( size_t off = temp.find( '.' ); off != string::npos;
				off = temp.find( '.', off+1 ) )
			{
				retval.push_back( temp.substr( last_offset, off - last_offset ) );
				last_offset = off + 1;
			}
			if ( last_offset < temp.size() )
			{
				retval.push_back( temp.substr( last_offset, temp.size() - last_offset ) );
			}
			return retval;
		}
		

		static void check_valid_numeric_cast_type( base_numeric_types::_enum val )
		{
			if ( val == base_numeric_types::no_known_type 
				|| val == base_numeric_types::i1 )
				throw runtime_error( "invalid numeric cast; either not a number of boolean" );
		}
	}
}

#endif