//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_OBJECT_TRAITS_H
#define CCLJ_OBJECT_TRAITS_H
#pragma once
#include "cclj/cclj.h"

namespace cclj
{

	template<typename obj_type>
	class default_object_traits
	{
	public:
		
		static uint8_t alignment() { return sizeof( void* ); }

		static void default_construct( obj_type* start, obj_type* end )
		{
			for_each( start, end, []( obj_type& item ) { new (&item)obj_type(); } );
		}

		static void copy_construct( obj_type* start, obj_type* end, const obj_type& init )
		{
			for_each( start, end, []( obj_type& item ) { new (&item)obj_type(init); } );
		}

		static void copy_construct( obj_type* dst_start, obj_type* dst_end
									, const obj_type* src_start )
		{
			for ( obj_type* iter = dst_start; iter != dst_end; ++iter, ++src_start )
			{
				new (iter) obj_type( *src_start );
			}
		}

		static void destruct( obj_type* start, obj_type* end )
		{
			for_each( start, end, 
				[]( obj_type& item )
			{
				item.~obj_type();
			} );
		}
		
		static void assign( obj_type* dst_start, obj_type* dst_end, const obj_type* src_start )
		{
			for ( obj_type* iter = dst_start; iter != dst_end; ++iter, ++src_start )
			{
				*iter =  *src_start;
			}
		}

		//src_start > dst_start;
		static void greater_overlap_assign( obj_type* dst_start, obj_type* dst_end, const obj_type* src_start )
		{
			//Note that src,end dst,dst_end could be overlapping.
			//Also not that dst is expected to be initialized in this case
			size_t num_items = dst_end - dst_start;
			for ( const obj_type* iter = dst_start; iter != dst_end; ++iter, ++src_start )
			{
				size_t idx = iter - dst_start;
				size_t ridx = num_items - idx - 1;
				//Assign in reverse to keep src from being overwritten.
				*(dst_start+ridx) = *(src_start + ridx);
			}
		}
		//src_start < dst_start, happens on vector::erase.
		static void lesser_overlap_assign( obj_type* dst_start, obj_type* dst_end, const obj_type* src_start )
		{
			assign( dst_start, dst_end, src_start );
		}
	};

	template<typename obj_type>
	class default_pod_traits
	{
	public:
		static uint8_t alignment() { return sizeof( void* ); }

		static void default_construct( obj_type* start, obj_type* end )
		{
			memset( start, 0, (end-start)*sizeof(obj_type) );
		}

		static void copy_construct( obj_type* start, obj_type* end, const obj_type& init )
		{
			for_each( start, end, []( obj_type& item ) { new (&item)obj_type(init); } );
		}

		static void copy_construct( obj_type* dst_start, obj_type* dst_end
									, const obj_type* src_start )
		{
			memcpy( dst_start, src_start, (dst_end - dst_start) * sizeof( obj_type ) );
		}

		static void destruct( obj_type*, obj_type* )
		{
		}
		
		static void assign( obj_type* dst_start, obj_type* dst_end, const obj_type* src_start )
		{
			memcpy( dst_start, src_start, (dst_end - dst_start) * sizeof( obj_type ) );
		}

		//src_start > dst_start;
		static void greater_overlap_assign( obj_type* dst_start, obj_type* dst_end, const obj_type* src_start )
		{
			memmove( dst_start, src_start, (dst_end - dst_start) * sizeof( obj_type ) );
		}
		static void lesser_overlap_assign( obj_type* dst_start, obj_type* dst_end, const obj_type* src_start )
		{
			memmove( dst_start, src_start, (dst_end - dst_start) * sizeof( obj_type ) );
		}
	};

	template<typename obj_type>
	class gc_static_traits
	{
	public:
		static void mark_references( obj_type& /*obj_type*/, mark_buffer& /*buffer*/ )
		{
		}
	};

}

#endif