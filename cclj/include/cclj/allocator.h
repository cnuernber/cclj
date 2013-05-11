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

	class allocator
	{
	protected:
		virtual ~allocator(){}
	public:
		friend class shared_ptr<allocator>;
		virtual void* allocate( size_t size, const char* file, int line ) = 0;
		virtual size_t get_alloc_size( void* ptr ) = 0;
		virtual void deallocate( void* memory ) = 0;

		//Create an allocator that, upon destruction, checks that everything has been
		//deallocator and asserts if this isn't the case.
		static shared_ptr<allocator> create_checking_allocator();
	};

	typedef shared_ptr<allocator> allocator_ptr;
}
#endif