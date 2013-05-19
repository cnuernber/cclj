//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_VIRTUAL_MACHINE_H
#define CCLJ_VIRTUAL_MACHINE_H
#include "cclj.h"
#include "garbage_collector.h"
#include "variant.h"

namespace cclj
{

	struct data_register
	{
		uint64_t _data;
		data_register(uint64_t input = 0 )
			: _data( input )
		{
		}
		~data_register(){}
	};

	struct gc_register
	{
		gc_obj_ptr _data;
		gc_register(gc_obj_ptr input = gc_obj_ptr() )
			: _data( input )
		{
		}
		~gc_register(){}
	};

	struct register_types
	{
		enum _enum
		{
			empty = 0,
			data,
			gc,
		};
	};

	template<typename tdtype>
	struct register_typeid
	{
	};

	template<> struct register_typeid<gc_register> { enum _enum { type = register_types::gc }; };
	template<> struct register_typeid<data_register> { enum _enum { type = register_types::data }; };


	struct register_traits
	{
		typedef register_types::_enum id_type;
		enum { buffer_size = sizeof(gc_register) };
		static id_type empty_type() { return register_types::empty; }
		template<typename tdtype>
		static register_types::_enum typeof() {	return static_cast<id_type>( register_typeid<tdtype>::type );  }

		template<typename trettype, typename tvisitor>
		static trettype visit( char* data, register_types::_enum type, tvisitor visitor )
		{
			switch( type )
			{
			case register_types::empty:
				return visitor();
			case register_types::data:
				return visitor( *reinterpret_cast<data_register*>( data ) );
			case register_types::gc:
				return visitor( *reinterpret_cast<gc_register*>( data ) );
			}
			throw runtime_error( "Failed to visit type" );
		}

		template<typename trettype, typename tvisitor>
		static trettype visit( const char* data, register_types::_enum type, tvisitor visitor )
		{
			switch( type )
			{
			case register_types::empty:
				return visitor();
			case register_types::data:
				return visitor( *reinterpret_cast<const data_register*>( data ) );
			case register_types::gc:
				return visitor( *reinterpret_cast<const gc_register*>( data ) );
			}
			throw runtime_error( "Failed to visit type" );
		}
	};

	class vm_register : public variant<variant_traits_impl<register_traits> >
	{
		typedef variant<variant_traits_impl<register_traits> > base_type;
	public:
		vm_register() {}
		vm_register(const data_register& _data) : base_type(_data) {}
		vm_register(const gc_register& _data) : base_type(_data) {}
		//unsafe; if exception is through this object could end up empty instead
		//of in a valid state.
		vm_register& operator=( const vm_register& other )
		{
			base_type::operator=( other );
			return *this;
		}
	};
	
	class virtual_machine
	{
	protected:
	public:

	};
}

#endif