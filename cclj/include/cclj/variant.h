//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_VARIANT_H
#define CCLJ_VARIANT_H
#include "cclj/cclj.h"
#include <cstdlib>

namespace cclj
{
	
	//until c++11 is completely support in vs 12, we have a hacked variant type.
	//Really, the only differences will be that the traits type will need to be more
	//fleshed out.
	template<typename traits_type>
	class variant
	{
		char _data[traits_type::buffer_size];
		typename traits_type::id_type _type_id;

		void destruct()
		{
			if (_type_id != traits_type::empty_type() )
			{
				traits_type::destruct( _data, _type_id );
				_type_id = traits_type::empty_type();
				memset( _data, 0, sizeof( _data ) );
			}
		}

		void copy_construct( const variant& other )
		{
			_type_id = other._type_id;
			if ( other._type_id != traits_type::empty_type() )
				traits_type::copy( _data, other._data, other._type_id );
			else
				memset( _data, 0, sizeof( _data ) );
		}

	public:
		variant() 
			: _type_id( traits_type::empty_type() )
		{
			memset( _data, 0, sizeof(_data) );
		}
		template<typename data_type>
		explicit variant( const data_type& dt )
			: _type_id( traits_type::template typeof<data_type>() )
		{
			static_assert( sizeof(dt) <= traits_type::buffer_size, "invalid type or buffer size" );
			traits_type::copy_construct( _data, dt );
		}
		variant( const variant& other )
		{
			copy_construct( other );
		}
		~variant()
		{
			destruct();
		}

		//unsafe; if exception is through this object could end up empty instead
		//of in a valid state.
		variant& operator=( const variant& other )
		{
			if ( &other != this )
			{
				destruct();
				copy_construct( other );
			}
			return *this;
		}

		typename traits_type::id_type type() const { return _type_id; }

		template<typename data_type>
		data_type& data() 
		{ 
			if ( _type_id == traits_type::template typeof<data_type>() )
				return *reinterpret_cast<data_type*>( _data );
			throw runtime_error( "Invalid cast" );
		}

		template<typename data_type>
		const data_type& data() const
		{ 
			if ( _type_id == traits_type::template typeof<data_type>() )
				return *reinterpret_cast<const data_type*>( _data );
			throw runtime_error( "Invalid cast" );
		}

		template<typename trettype, typename tvisitor>
		trettype visit( tvisitor visitor )
		{
			return traits_type::visit( _data, _type_id, visitor );
		}

		template<typename trettype, typename tvisitor>
		trettype visit( tvisitor visitor ) const
		{
			return traits_type::visit( _data, _type_id, visitor );
		}

		bool empty() const { return _type_id == traits_type::empty_type(); }
	};

	
	template<typename tdtype>
	struct variant_copy_construct
	{
		void operator()( char* data, const tdtype& type )
		{
			new(data)tdtype( type );
		}
	};

	template<typename tdtype>
	struct variant_destruct
	{
		void operator()( tdtype& type )
		{
			type.~tdtype();
		}
	};

	struct variant_copier
	{
		char* _data;
		variant_copier( char* data ) : _data( data ) {}
		template<typename tdtype>
		void operator()( const tdtype& type )
		{
			variant_copy_construct<tdtype>()( _data, type );
		}
		void operator()(){}
	};

	struct variant_destructor
	{
		template<typename tdtype>
		void operator()( tdtype& type )
		{
			variant_destruct<tdtype>()( type );
		}
		void operator()()
		{
		}
	};

	
	template<typename trettype,typename tvisitortype>
	struct const_visitor
	{
		tvisitortype _visitor;
		const_visitor( const tvisitortype& vs ) : _visitor( vs ) {}
		template<typename tdtype>
		trettype operator()( const tdtype& type )
		{
			return _visitor( type );
		}
		
		trettype operator()()
		{
			return _visitor();
		}
	};

	template<typename tbasetype>
	struct variant_traits_impl : public tbasetype
	{
		template<typename tdtype>
		static void copy_construct( char* data, const tdtype& type )
		{
			variant_copy_construct<tdtype>()( data, type );
		}

		static void copy( char* data, const char* src, typename tbasetype::id_type type )
		{
			visit<void>( src, type, variant_copier(data) );
		}

		static void destruct( char* data, typename tbasetype::id_type type )
		{
			visit<void>( data, type, variant_destructor() );
		}
		
		template<typename trettype, typename tvisitor>
		static trettype visit( char* data, typename tbasetype::id_type type, tvisitor visitor )
		{
			return tbasetype::template do_visit<trettype>( data, type, visitor );
		}

		//so base classes only have to define one visit type.
		template<typename trettype, typename tvisitor>
		static trettype visit( const char* data, typename tbasetype::id_type type, tvisitor visitor )
		{
			return tbasetype::template do_visit<trettype>( const_cast<char*>(data), type, const_visitor<trettype,tvisitor>(visitor) );
		}
	};
}

#endif
