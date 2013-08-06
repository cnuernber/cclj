//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/lisp_types.h"


using namespace cclj;
using namespace cclj::lisp;

namespace  {

	struct type_map_key
	{
		string_table_str	_name;
		type_ref_ptr_buffer _specializations;
		size_t				_hash_code;
		type_map_key( string_table_str n, type_ref_ptr_buffer s )
			: _name( n )
			, _specializations( s )
		{
			_hash_code = std::hash<string_table_str>()( _name );
			for_each( _specializations.begin(), _specializations.end(), [this]
			( type_ref_ptr ref )
			{
				_hash_code = _hash_code ^ reinterpret_cast<size_t>( ref );
			} );
		}

		bool operator==( const type_map_key& other ) const
		{
			if ( _name == other._name )
			{
				if ( _specializations.size() == other._specializations.size() )
				{
					for ( size_t idx = 0, end = _specializations.size(); idx < end; ++idx )
					{
						if ( _specializations[idx] != other._specializations[idx] )
							return false;
					}
					return true;
				}
			}
			return false;
		}
	};
}

namespace std
{
	template<> struct hash<type_map_key>
	{
		size_t operator()( const type_map_key& k ) const { return k._hash_code; }
	};
}

namespace {

	class type_library_impl : public type_library
	{
		allocator_ptr		_allocator;
		string_table_ptr	_str_table;
		typedef unordered_map<type_map_key, type_ref_ptr> type_map;
		type_map _types;
	public:
		type_library_impl( allocator_ptr alloc, string_table_ptr str_t )
			: _allocator( alloc )
			, _str_table( str_t )
		{
		}

		~type_library_impl()
		{
			for_each( _types.begin(), _types.end(), 
			[this](type_map::value_type& type )
			{
				_allocator->deallocate( type.second );
			} );
		}
		
		virtual string_table_ptr string_table() { return _str_table; }

		virtual type_ref& get_type_ref( string_table_str name, type_ref_ptr_buffer _specializations )
		{
			type_map_key theKey( name, _specializations );
			type_map::iterator iter = _types.find( theKey );
			if ( iter != _types.end() ) return *iter->second;
			size_t type_size = sizeof( type_ref );
			size_t array_size = sizeof( type_ref_ptr ) * _specializations.size();
			uint8_t* mem = _allocator->allocate( type_size + array_size, sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() );
			type_ref* retval = reinterpret_cast<type_ref*>( mem );
			type_ref_ptr* array_data = reinterpret_cast<type_ref_ptr*>( mem + type_size );
			new (retval) type_ref();
			retval->_name = name;
			if ( array_size ) {
				memcpy( array_data, _specializations.begin(), array_size );
				retval->_specializations = type_ref_ptr_buffer( array_data, _specializations.size() );
			}
			theKey = type_map_key( name, retval->_specializations );
			_types.insert( make_pair( theKey, retval ) );
			return *retval;
		}
	};
}

type_library_ptr type_library::create_type_library( allocator_ptr allocator, string_table_ptr s )
{
	return make_shared<type_library_impl>( allocator, s );
}

string type_ref::to_string()
{
	string retval;
	retval.append( _name.c_str() );
	if ( _specializations.size() )
	{
		retval.append( "[" );

		bool first = true;
		for_each( _specializations.begin(), _specializations.end(), [&]
		( type_ref_ptr ref )
		{
				retval.append( " " );
			first = false;
			retval.append( ref->to_string() ); 
		} );
		retval.append( "]" );
	}
	return retval;
}
