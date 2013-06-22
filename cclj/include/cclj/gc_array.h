//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_GC_ARRAY_H
#define CCLJ_GC_ARRAY_H
#pragma once
#include "cclj/garbage_collector.h"
#include "cclj/algo_util.h"

//You would need to use quicksort or know the exact type and use std::sort to sort the array
//there aren't facilities to generalized sort an array based off of a generic comparator unless
//its type is known.  c-quicksort requires a stand-alone function so comparators with state are 
//impossible and std::sort won't work because fundamentally the item the iterator produces (array_ref) can't
//be a reference or pointer type.

namespace cclj {

	template<typename obj_type>
	class default_object_traits
	{
	public:

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
			for ( const obj_type* iter = dst_start; iter != dst_end; ++iter, ++src_start )
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
			for ( const obj_type* iter = dst_start; iter != dst_end; ++iter, ++src_start )
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
		static uint32_t object_reference_count() { return 0; }
		static gc_object* next_object_reference( obj_type& /*obj_type*/, uint32_t /*idx*/ )
		{
			return nullptr;
		}
	};

	template<typename obj_type>
	class gc_array_traits : public default_object_traits<obj_type>, public gc_static_traits<obj_type>
	{
	public:
		enum
		{
			alignment = sizeof(void*),
		};
	};


	template<typename obj_type, uint8_t alignment = gc_array_traits<obj_type>::alignment>
	class gc_array : public gc_object
	{
	public:
		
		typedef obj_type* iterator;
		typedef const obj_type* const_iterator;
		typedef gc_array<obj_type> this_type;
		typedef gc_scoped_lock<this_type> this_ptr_type;
		typedef gc_array_traits<obj_type> traits_type;

	private:
		garbage_collector_ptr	_gc;
		file_info				_file_info;
		obj_type*				_data;
		obj_type*				_data_end;
		obj_type*				_data_capacity;

		void initialize( garbage_collector_ptr gc )
		{
			_gc = gc;
			_data			= nullptr;
			_data_end		= nullptr;
			_data_capacity	= nullptr;
		}

	public:
		
		typedef obj_type* iterator;
		typedef const obj_type* const_iterator;
		typedef gc_array<obj_type> this_type;
		typedef gc_scoped_lock<this_type> this_ptr_type;
		typedef gc_array_traits<obj_type> traits_type;

		static object_constructor create_constructor(garbage_collector_ptr gc, file_info location)
		{
			return [=]( uint8_t* mem, size_t)
			{
				this_type* data = new (mem) this_type(gc, location);
				return data;
			};
		}

		static this_ptr_type create( garbage_collector_ptr gc, file_info location )
		{
			gc_object& retval = gc->allocate_object( sizeof( this_type )
									, alignment, create_constructor( gc, location )
									, location );
			return this_ptr_type( gc, reinterpret_cast<this_type&>( retval ) );
		}


		gc_array(file_info = CCLJ_IMMEDIATE_FILE_INFO() )
		: _file_info( file_info )
		{ initialize(garbage_collector_ptr()); }

		gc_array( garbage_collector_ptr gc, file_info info )
			: _file_info( info )
		{
			initialize( gc );
		}

		

		gc_array( const this_type& obj )
		{
			initialize( obj._gc );
			_file_info = obj._file_info;
			size_t num_items = obj.size();
			if ( num_items )
			{
				reserve( num_items );
				_data_end = _data_capacity;
				traits_type::copy_construct( obj._data, obj._data_end, _data );
			}
		}

		gc_array& operator=( const this_type& obj )
		{
			if ( this != &obj )
			{
				if ( _gc != obj._gc )
				{
					if ( _gc ) throw runtime_error( "failed to handle gc issues" );
					_gc = obj._gc;
				}

				resize( 0 );
				if ( obj.size() )
					insert( end(), obj.begin(), obj.end() );
			}
			return *this;
		}

		virtual ~gc_array()
		{
			if ( _data )
				_gc->deallocate( _data );
			_data = nullptr;
			_data_end = nullptr;
			_data_capacity = nullptr;
			_gc = garbage_collector_ptr();
		}

		size_t size() const { return _data_end - _data; }
		size_t capacity() const { return _data_capacity - _data; }
		bool empty() const { return _data_end == _data; }
		obj_type* data() { return _data; }
		const obj_type* data() const { return _data; }

		iterator begin() 
		{
			return _data;
		}
		iterator end() 
		{
			return _data_end;
		}
		
		const_iterator begin() const
		{
			return const_cast<gc_array&>( *this ).begin();
		}

		const_iterator end() const
		{
			return const_cast<gc_array&>( *this ).end();
		}

		obj_type& operator[]( int32_t idx ) 
		{ 
			if ( idx >= (int32_t)size() )
				throw runtime_error( "array access out of bounds" );
			return _data[idx];
		}
		
		const obj_type& operator[]( int32_t idx ) const
		{
			return const_cast<gc_array&>( *this ).operator[]( idx );
		}

		void reserve( size_t total )
		{
			if ( total <= capacity() ) return;
			
			size_t item_size = align_number( sizeof( obj_type ), alignment );
			obj_type* new_data = reinterpret_cast<obj_type*>( _gc->allocate( item_size*total, alignment, _file_info ) );
			size_t old_size = size();
			if ( old_size )
			{
				traits_type::copy_construct( new_data, new_data + old_size, _data );
				traits_type::destruct( new_data, new_data + old_size );
			}

			if ( _data )
				_gc->deallocate( _data );

			_data = new_data;
			_data_end = new_data + old_size;
			_data_capacity = new_data + total;
		}

		iterator insert_uninitialized( iterator _where, size_t num_items )
		{
			bool at_end = _where == end();
			if ( num_items == 0 )
				return _where;
			size_t offset = _where - begin();
			size_t required = num_items;
			if ( capacity() < required )
				reserve( (capacity() + num_items)*2 );
			_where = begin() + offset;
			iterator old_end = end();
			iterator end_insert = _where + num_items;

			_data_end += num_items;
			//move items to create a hole

			if (!at_end)
			{
				traits_type::greater_overlap_assign( _where, old_end, end_insert );
				traits_type::destruct( _where, std::min( end_insert, old_end ) );
			}

			return _where;
		}

		void insert( iterator _where, const_iterator start_item, const_iterator end_item )
		{
			size_t num_items = end_item - start_item;
			_where = insert_uninitialized( _where, num_items );
			iterator end_insert = _where + num_items;
			//now fill the hole
			traits_type::copy_construct( _where, end_insert, start_item );
		}
		
		//insert using default initializer
		void insert( iterator _where, size_t num_items )
		{
			_where = insert_uninitialized( _where, num_items );
			iterator end_insert = _where + num_items;
			//now fill the hole
			traits_type::default_construct( _where, end_insert );
		}

		
		//insert using default initializer
		void insert( iterator _where, size_t num_items, const obj_type& initializer )
		{
			_where = insert_uninitialized( _where, num_items );
			iterator end_insert = _where + num_items;
			//now fill the hole
			traits_type::copy_construct( _where, end_insert, initializer );
		}

		void erase( iterator start, iterator stop )
		{
			//Copy the section after stop to start
			//then adjust size.
			if( stop < start )
				throw runtime_error( "what the hell do you think you are doing? stop is less that start..." );
			if ( stop == start )
				return;

			int32_t num_items = stop - start;
			if ( num_items )
			{
				traits_type::destruct( start, stop );
				size_t offset = end() - stop;
				if ( offset )
					traits_type::lesser_overlap_assign( start, start + offset, stop );
			}
			_data_end -= num_items;
		}

		void resize( size_t new_size )
		{
			if ( new_size > size() )
			{
				insert( end(), new_size - size() );
			}
			else if ( new_size < size() )
			{
				erase( begin() + (int)new_size, end() );
			}
		}

		struct gc_array_ref_data_type
		{
			typename gc_array<obj_type>::iterator	_ref_iter;
			uint32_t									_ref_index;
			gc_array_ref_data_type( typename gc_array<obj_type>::iterator iter )
				: _ref_iter( iter )
				, _ref_index( 0 )
			{
			}
		};

		typedef gc_array_ref_data_type gc_array_ref_data;
		
		virtual alloc_info get_gc_refdata_alloc_info() { return alloc_info( sizeof( gc_array_ref_data ), sizeof(void*) ); }
		virtual void initialize_gc_refdata( uint8_t* data )
		{
			new (data) gc_array_ref_data( begin() );
		}

		//gc integration
		virtual uint32_t get_gc_references( gc_object** buffer, uint32_t bufsize, uint32_t /*obj_index*/, uint8_t* refdata )
		{
			gc_array_ref_data& ref_data = *reinterpret_cast<gc_array_ref_data*>( refdata );

			iterator end_iter = end();
			
			uint32_t retval = 0;
			uint32_t num_refs = traits_type::object_reference_count();
			gc_object** buffer_end = buffer + bufsize;
			for ( ; ref_data._ref_iter != end_iter && buffer != buffer_end; ++ref_data._ref_iter )
			{
				for( ; ref_data._ref_index < num_refs && buffer != buffer_end; ++ref_data._ref_index )
				{
					auto next_ref = traits_type::next_object_reference( *ref_data._ref_iter, ref_data._ref_index );
					if ( next_ref )
					{
						*buffer = next_ref;
						++retval;			
						++buffer;
					}
				}
				ref_data._ref_index = 0;
				//have to break here so we don't increment ref_iter in this case before the boolean check.
				if ( buffer == buffer_end )
					break;
			}
			return retval;
		}
	};
}

#endif