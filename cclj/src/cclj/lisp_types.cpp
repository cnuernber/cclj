//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/lisp_types.h"
#include "cclj/pool.h"


using namespace cclj;
using namespace cclj::lisp;

namespace  {
	class factory_impl : public factory
	{
		allocator_ptr			_allocator;
		pool<sizeof(type_ref)>	_object_pool;
		const cons_cell&		_empty_cell;
		vector<uint8_t*>		_buffer_allocs;

	public:
		factory_impl( allocator_ptr alloc, const cons_cell& empty_cell )
			: _allocator( alloc )
			, _object_pool( alloc, CCLJ_IMMEDIATE_FILE_INFO(), sizeof(void*) )
			, _empty_cell( empty_cell )
		{
		}
		~factory_impl()
		{
			for_each( _buffer_allocs.begin(), _buffer_allocs.end(),
				[this]( uint8_t* alloc )
			{
				_allocator->deallocate( alloc );
			} );
		}
		
		virtual const cons_cell& empty_cell() { return _empty_cell; }
		virtual cons_cell* create_cell() { return _object_pool.construct<cons_cell>(); }
		virtual array* create_array()  { return _object_pool.construct<array>(); }
		virtual symbol* create_symbol() { return _object_pool.construct<symbol>(); }
		virtual constant* create_constant() { return _object_pool.construct<constant>(); }
		virtual uint8_t* allocate_data( size_t size, uint8_t alignment )
		{
			if ( size <= sizeof(type_ref ) && alignment < sizeof(void*) )
			{
				return _object_pool.allocate();
			}
			else
			{
				uint8_t* retval = _allocator->allocate( size, alignment, CCLJ_IMMEDIATE_FILE_INFO() );
				_buffer_allocs.push_back( retval );
				return retval;
			}
		}
		virtual object_ptr_buffer allocate_obj_buffer(size_t size)
		{
			if ( size == 0 )
				return object_ptr_buffer();
			object_ptr* new_data = reinterpret_cast<object_ptr*>( 
				_allocator->allocate( size * sizeof( object_ptr* ), sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() ) );
			memset( new_data, 0, size * sizeof( object_ptr* ) );
			_buffer_allocs.push_back( reinterpret_cast<uint8_t*>( new_data ) );
			return object_ptr_buffer( new_data, size );
		}
	};
}

factory_ptr factory::create_factory( allocator_ptr allocator, const cons_cell& empty_cell )
{
	return make_shared<factory_impl>( allocator, empty_cell );
}