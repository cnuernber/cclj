//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/allocator.h"
#include "cclj/variant.h"
#include "cclj/algo_util.h"

using namespace cclj;

namespace {
	struct file_line_info
	{
		string file;
		int line;
		file_line_info( const string& f, int l )
			: file( f ), line( l )
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

		virtual void* allocate( size_t size, uint8_t alignment, const char* file, int line )
		{
			if (!size ) return nullptr;
			size_t newsize = size + sizeof(alloc_info);
			if ( alignment > 4 )
				newsize = newsize + alignment - 1;

			void* newmem = malloc(newsize);
			alloc_info* memPtr = (alloc_info*)newmem;
			memPtr->alloc_size = size;
			memPtr->alignment = alignment;
			size_t original_ptr = reinterpret_cast<size_t>(newmem);
			size_t ptr_val = original_ptr;
			ptr_val += sizeof( alloc_info );
			ptr_val = align_number( ptr_val, alignment );
			size_t backptr_val = ptr_val - 1;
			uint8_t* backdata_ptr = reinterpret_cast<uint8_t*>( backptr_val );
			uint8_t offset = static_cast<uint8_t>( ptr_val - original_ptr );
			backdata_ptr[0] = offset;
			void* retval = reinterpret_cast<void*>( backdata_ptr + 1 );
			outstanding_allocations.insert( make_pair( retval, file_line_info( non_null( file ), line ) ) );
			return retval;
		}

		alloc_info* get_original_ptr( void* user_ptr )
		{
			if ( user_ptr == nullptr ) return nullptr;
			uint8_t* user_byte_ptr = reinterpret_cast<uint8_t*>( user_ptr );
			uint8_t* offset_ptr = user_byte_ptr - 1;
			uint8_t offset = *offset_ptr;
			uint8_t* original_ptr = user_byte_ptr - offset;
			return reinterpret_cast<alloc_info*>( original_ptr );
		}

		virtual alloc_info get_alloc_info( void* ptr )
		{
			alloc_info* alloc_ptr = get_original_ptr( ptr );
			if ( alloc_ptr )
				return *alloc_ptr;

			return alloc_info();
		}

		virtual void deallocate( void* memory )
		{
			if ( !memory ) return;
			ptr_to_info_map::iterator iter = outstanding_allocations.find( memory );
			if ( iter != outstanding_allocations.end() )
			{
				alloc_info* info = get_original_ptr( memory );
				free( info );
				outstanding_allocations.erase( iter );
			}
			else
			{
				throw std::runtime_error( "deallocating memory that was not allocated" );
			}
		}
		
		virtual bool check_ptr( void* ptr )
		{
			return outstanding_allocations.find( ptr ) != outstanding_allocations.end();
		}
	};
}

allocator_ptr allocator::create_checking_allocator()
{
	return make_shared<tracking_alloc>();
}