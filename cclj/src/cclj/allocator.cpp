//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/allocator.h"

using namespace cclj;

namespace {
	struct file_line_info
	{
		string file;
		int line;
		size_t size;
		file_line_info( const string& f, int l, size_t s )
			: file( f ), line( l ), size( s )
		{
		}
	};

	const char* non_null( const char* ptr ) { if ( !ptr ) return ""; return ptr; }

	typedef unordered_map<void*, file_line_info> ptr_to_info_map;

	struct tracking_alloc : public allocator
	{
		ptr_to_info_map outstanding_allocations;

		~tracking_alloc()
		{
			assert( outstanding_allocations.empty() );
		}

		virtual void* allocate( size_t size, const char* file, int line )
		{
			if (!size ) return nullptr;
			size_t newsize = size + sizeof(size_t);
			void* newmem = malloc(newsize);
			size_t* memPtr = (size_t*)newmem;
			memPtr[0] = size;
			void* retval = memPtr + 1;
			outstanding_allocations.insert( make_pair( retval, file_line_info( non_null( file ), line, size ) ) );
			return retval;
		}

		virtual size_t get_alloc_size( void* ptr )
		{
			size_t* memPtr = (size_t*)ptr;
			--memPtr;
			return memPtr[0];
		}

		virtual void deallocate( void* memory )
		{
			if ( !memory ) return;
			ptr_to_info_map::iterator iter = outstanding_allocations.find( memory );
			if ( iter != outstanding_allocations.end() )
			{
				size_t* memPtr = (size_t*)memory;
				--memPtr;
				free( memPtr );
				outstanding_allocations.erase( iter );
			}
			else
			{
				throw std::runtime_error( "deallocating memory that was not allocated" );
			}
		}
	};
}

allocator_ptr allocator::create_checking_allocator()
{
	return make_shared<tracking_alloc>();
}