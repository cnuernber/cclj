//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_POOL_H
#define CCLJ_POOL_H

#pragma once
#include "cclj/cclj.h"
#include "cclj/allocator.h"
#include "cclj/noncopyable.h"
#include "cclj/algo_util.h"

namespace cclj
{
	template<uint32_t item_size, uint32_t slab_size = 4096>
	class pool : public noncopyable
	{
		struct free_entry
		{
			free_entry* _next;
		};

		free_entry*			_free_list;
		vector<uint8_t*>	_slab_list;
		allocator_ptr		_allocator;
		file_info			_alloc_info;
		uint8_t				_item_alignment;

		void allocate_slab()
		{
			uint32_t actual_slab_size = align_number( slab_size, _item_alignment );
			uint8_t* new_slab = _allocator->allocate( actual_slab_size, _item_alignment, _alloc_info );

			uint32_t actual_item_size = align_number( std::max( item_size, sizeof( free_entry ) ), _item_alignment );

			uint32_t num_items = actual_slab_size / actual_item_size;
			uint8_t* item_ptr = new_slab;

			for ( uint32_t idx = 0; idx < num_items; ++idx )
			{
				add_free_entry( item_ptr );
				item_ptr += actual_item_size;
			}
			_slab_list.push_back( new_slab );
		}

		void add_free_entry( void* data )
		{
			if ( data == nullptr ) return;

			free_entry* new_entry = reinterpret_cast<free_entry*>( data );
			new_entry->_next = _free_list;
			_free_list = new_entry;
		}

	public:
		pool( allocator_ptr alloc, const file_info& alloc_info, uint8_t item_alignment = sizeof(void*) ) 
			: _free_list( nullptr )
			, _allocator( alloc )
			, _alloc_info( alloc_info ) 
			, _item_alignment( item_alignment )
		{}
		~pool()
		{
			for_each( _slab_list.begin(), _slab_list.end(),
				[this]( uint8_t* slab )
			{
				_allocator->deallocate( slab );
			} );
		}
		uint8_t* allocate()
		{
			if ( !_free_list )
				allocate_slab();

			if ( _free_list )
			{
				uint8_t* retval = reinterpret_cast<uint8_t*>( _free_list );
				_free_list = _free_list->_next;
				return retval;
			}
			return nullptr;
		}

		void deallocate( void* data )
		{
			add_free_entry( data );
		}


		//Utility functions for creating/destroying objects.

		template<typename item_type>
		item_type* construct()
		{
			static_assert(sizeof(item_type) <= item_size, "invalid pool construction");
			item_type* retval = reinterpret_cast<item_type*>( allocate() );
			return new (retval) item_type();
		}
		
		//constructors overloaded for up to 4 args
		template<typename item_type, typename a0>
		item_type* construct(const a0& arg)
		{
			static_assert(sizeof(item_type) <= item_size, "invalid pool construction");
			item_type* retval = reinterpret_cast<item_type*>( allocate() );
			return new (retval) item_type(arg);
		}

		template<typename item_type, typename a0, typename a1>
		item_type* construct(const a0& arg0, const a1& arg1)
		{
			static_assert(sizeof(item_type) <= item_size, "invalid pool construction");
			item_type* retval = reinterpret_cast<item_type*>( allocate() );
			return new (retval) item_type(arg0, arg1);
		}
		
		template<typename item_type, typename a0, typename a1, typename a2>
		item_type* construct(const a0& arg0, const a1& arg1, const a2& arg2)
		{
			static_assert(sizeof(item_type) <= item_size, "invalid pool construction");
			item_type* retval = reinterpret_cast<item_type*>( allocate() );
			return new (retval) item_type(arg0, arg1, arg2);
		}
		
		template<typename item_type, typename a0, typename a1, typename a2, typename a3>
		item_type* construct(const a0& arg0, const a1& arg1, const a2& arg2, const a3& arg3)
		{
			static_assert(sizeof(item_type) <= item_size, "invalid pool construction");
			item_type* retval = reinterpret_cast<item_type*>( allocate() );
			return new (retval) item_type(arg0, arg1, arg2, arg3);
		}

		template<typename item_type>
		void destruct( item_type* item )
		{
			static_assert(sizeof(item_type) <= item_size, "invalid pool construction");
			if ( item == nullptr ) return;
			item->~item_type();
			deallocate( item );
		}

	};
}

#endif