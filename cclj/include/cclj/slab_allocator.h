//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_SLAB_ALLOCATOR_H
#define CCLJ_SLAB_ALLOCATOR_H
#pragma once

#include "cclj/cclj.h"
#include "cclj/allocator.h"
#include "cclj/algo_util.h"

namespace cclj
{
	template<uint32_t slab_size = 4096>
	class slab_allocator
	{
		allocator_ptr _allocator;
		vector<uint8_t*> _allocations;
		size_t _freesize;
	public:
		slab_allocator( allocator_ptr alloc )
			: _allocator( alloc )
			, _freesize( 0 )
		{
		}
		~slab_allocator()
		{
			for_each( _allocations.begin(), _allocations.end(), [this]
			( uint8_t* alloc )
			{
				_allocator->deallocate( alloc );
			} );
		}

		uint8_t* allocate( size_t size, uint8_t alignment, file_info alloc_info )
		{
			if ( size < sizeof( void* ) )
				size = sizeof( void* );
			size_t safe_size = size;
			if ( alignment > sizeof(void*) )
				safe_size = size + alignment - 1;
			if ( safe_size < _freesize )
			{
				size_t offset = slab_size - _freesize;
				uint8_t* current = _allocations.back();
				_freesize -= safe_size;
				uint8_t* retval = current + offset;
				size_t retval_item = reinterpret_cast<size_t>( retval );
				return reinterpret_cast<uint8_t*>( align_number( retval_item, alignment ) );
			}
			if ( safe_size < slab_size )
			{
				_allocations.push_back( _allocator->allocate( slab_size, sizeof(void*), alloc_info ) );
				_freesize = slab_size;
				return allocate( size, alignment, alloc_info );
			}
			//else large allocation, allocate, record, and preserve invariant that the last open
			//slab is at the end.
			{
				_allocations.push_back( _allocator->allocate( size, alignment, alloc_info ) );
				uint8_t* retval = _allocations.back();
				if ( _freesize )
				{
					//swap the end and the second to end
					swap( *(_allocations.end() - 1), *(_allocations.end() - 2) );
				}
				return retval;
			}
		}
		
		//Utility functions for creating/destroying objects.

		template<typename item_type>
		item_type* construct()
		{
			item_type* retval = reinterpret_cast<item_type*>( allocate(sizeof(item_type), sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() ) );
			return new (retval) item_type();
		}
		
		//constructors overloaded for up to 4 args
		template<typename item_type, typename a0>
		item_type* construct(const a0& arg)
		{
			item_type* retval = reinterpret_cast<item_type*>( allocate(sizeof(item_type), sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() ) );
			return new (retval) item_type(arg);
		}

		template<typename item_type, typename a0, typename a1>
		item_type* construct(const a0& arg0, const a1& arg1)
		{
			item_type* retval = reinterpret_cast<item_type*>( allocate(sizeof(item_type), sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() ) );
			return new (retval) item_type(arg0, arg1);
		}
		
		template<typename item_type, typename a0, typename a1, typename a2>
		item_type* construct(const a0& arg0, const a1& arg1, const a2& arg2)
		{
			item_type* retval = reinterpret_cast<item_type*>( allocate(sizeof(item_type), sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() ) );
			return new (retval) item_type(arg0, arg1, arg2);
		}
		
		template<typename item_type, typename a0, typename a1, typename a2, typename a3>
		item_type* construct(const a0& arg0, const a1& arg1, const a2& arg2, const a3& arg3)
		{
			item_type* retval = reinterpret_cast<item_type*>( allocate(sizeof(item_type), sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() ) );
			return new (retval) item_type(arg0, arg1, arg2, arg3);
		}
	};

}


#endif