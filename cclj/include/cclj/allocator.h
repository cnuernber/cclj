//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_ALLOCATOR_H
#define CCLJ_ALLOCATOR_H
#pragma once
#include "cclj/cclj.h"

namespace cclj {
	
	struct alloc_info
	{
		size_t	alloc_size;
		uint8_t alignment;
		alloc_info() : alloc_size( 0 ), alignment( 0 ) {}
	};

	class file_info
	{
	public:
		const char* file;
		int line;
		file_info( const char* f, int l )
			: file( f ? f : "" )
			, line( l )
		{
		}
		file_info() : file( "" ), line( 0 ) {}
	};

#define CCLJ_IMMEDIATE_FILE_INFO() file_info( __FILE__, __LINE__ )

	class allocator
	{
	protected:
		virtual ~allocator(){}
	public:
		friend class shared_ptr<allocator>;
		virtual uint8_t* allocate( size_t size, uint8_t alignment, file_info alloc_info ) = 0;
		virtual alloc_info get_alloc_info( void* ptr ) = 0;
		virtual void deallocate( void* memory ) = 0;
		//Check if this is a valid ptr.  Returns true if this allocate knows about the pointer
		//or false otherwise.
		virtual bool check_ptr( void* ptr ) = 0;

		//Create an allocator that, upon destruction, checks that everything has been
		//deallocator and asserts if this isn't the case.
		static shared_ptr<allocator> create_checking_allocator();
	};

	typedef shared_ptr<allocator> allocator_ptr;
	

	class scoped_allocation
	{
		allocator_ptr	_allocator;
		uint8_t*		_data;
	public:
		scoped_allocation( allocator_ptr allocator, uint8_t* data )
			: _allocator( allocator ), _data( data )
		{
		}
		uint8_t* data() { return _data; }
		void forget() { _data = nullptr; }
		~scoped_allocation()
		{
			if ( _data )
				_allocator->deallocate( _data );
		}

	};
}
#endif