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
#include "garbage_collector.h"
#include "algo_util.h"

namespace cclj
{
	
	
	class data_array_iter_base
	{
		uint32_t	_count;
		int32_t		_index;
	public:
		data_array_iter_base( uint32_t _c = 0, int32_t idx = 0 )
			: _count( _c )
			, _index( idx )
		{
		}
		data_array_iter_base( const data_array_iter_base& other )
			: _count( other._count )
			, _index( other._index )
		{
		}
		data_array_iter_base& operator=( const data_array_iter_base& other )
		{
			_count = other._count;
			_index = other._index;
			return *this;
		}

		data_array_iter_base operator+( int val )
		{
			return data_array_iter_base( _count, _index + val );
		}

		data_array_iter_base operator-( int val )
		{
			return data_array_iter_base( _count, _index - val );
		}
		
		data_array_iter_base operator+( data_array_iter_base val )
		{
			return data_array_iter_base( _count, _index + val._index );
		}

		data_array_iter_base operator-( data_array_iter_base val )
		{
			return data_array_iter_base( _count, _index - val._index );
		}

		bool operator==( const data_array_iter_base& other ) const
		{
			return _index == other._index;
		}
		
		bool operator!=( const data_array_iter_base& other ) const
		{
			return _index != other._index;
		}

		bool operator > ( const data_array_iter_base& other ) const
		{
			return _index > other._index;
		}
		
		bool operator < ( const data_array_iter_base& other ) const
		{
			return _index < other._index;
		}

		bool operator >= ( const data_array_iter_base& other ) const
		{
			return _index >= other._index;
		}
		
		bool operator <= ( const data_array_iter_base& other ) const
		{
			return _index <= other._index;
		}

		data_array_iter_base& operator++() { ++_index; return *this; }
		data_array_iter_base operator++(int) 
		{ 
			data_array_iter_base retval (*this );
			++_index;
			return retval;
		}
		
		data_array_iter_base& operator--() { --_index; return *this; }
		data_array_iter_base operator--(int) 
		{ 
			data_array_iter_base retval (*this );
			--_index;
			return retval;
		}

		data_array_iter_base operator+=(int idx ) { _index += idx; return *this; }
		data_array_iter_base operator-=(int idx ) { _index -= idx; return *this; }
		data_array_iter_base operator/=(int idx ) { _index /= idx; return *this; }
		data_array_iter_base operator*=(int idx ) { _index *= idx; return *this; }

		int32_t index() const { return _index; }
		uint32_t count() const { return _count; }

		operator int32_t () const { return _index; }
	};

	struct gc_array_const_item_ref
	{
		class_definition*		_definition;
		uint32_t				_instance_size;
		const uint8_t*			_item_data;
		gc_array_const_item_ref( uint32_t is = 0, const uint8_t* id = nullptr, class_definition* cls_def = nullptr )
			: _instance_size( is )
			, _item_data( id )
			, _definition( cls_def )
		{
		}
	};

	struct gc_array_item_ref
	{
		class_definition*		_definition;
		uint32_t				_instance_size;
		uint8_t*				_item_data;
		gc_array_item_ref( uint32_t is = 0, uint8_t* id = nullptr, class_definition* cls_def = nullptr )
			: _instance_size( is )
			, _item_data( id )
			, _definition( cls_def )
		{
		}

		operator gc_array_const_item_ref () const
		{
			return gc_array_const_item_ref( _instance_size, _item_data, _definition );
		}
	};

}

namespace std
{
	void swap( cclj::gc_array_item_ref& lhs, cclj::gc_array_item_ref& rhs )
	{
		if ( lhs._definition != rhs._definition
			|| lhs._instance_size != rhs._instance_size )
			throw runtime_error( "mismatched objects in swap" );
		if ( ( lhs._instance_size % 4 ) != 0 )
			throw runtime_error( "instance sizes must be a multiple 4" );
		uint32_t num_items = lhs._instance_size / 4;
		uint32_t* lhs_ptr = reinterpret_cast<uint32_t*>( lhs._item_data );
		uint32_t* rhs_ptr = reinterpret_cast<uint32_t*>( rhs._item_data );
		for( uint32_t offset = 0; offset < num_items; ++offset )
		{
			std::swap( lhs_ptr[offset], rhs_ptr[offset] );
		}
	}
}

namespace cclj
{

	class gc_array_const_iter : public data_array_iter_base
	{
		class_definition*		_definition;
		uint32_t				_instance_size;
		const uint8_t*			_array_data;
	public:

		gc_array_const_iter() : data_array_iter_base(), _definition( nullptr ), _instance_size( 0 ), _array_data( 0 ) {}
		gc_array_const_iter( const data_array_iter_base& iter_base, class_definition* def
								, uint32_t is, const uint8_t* data )
			: data_array_iter_base( iter_base )
			, _definition( def )
			, _instance_size( is )
			, _array_data( data )
		{
		}
		gc_array_const_iter( const gc_array_const_iter& other )
			: data_array_iter_base( other )
			, _definition( other._definition )
			, _instance_size( other._instance_size )
			, _array_data( other._array_data )
		{
		}
		
		gc_array_const_iter& operator=( const gc_array_const_iter& other )
		{
			if ( this != &other )
			{
				data_array_iter_base::operator=( other );
				_definition = other._definition;
				_instance_size = other._instance_size;
				_array_data = other._array_data;
			}
			return *this;
		}

		gc_array_const_item_ref item_ref()
		{
			if ( index() >= (int32_t)count() 
				|| index() < 0 )
				throw runtime_error( "out of bounds array access" );
			int32_t off = index() * (int32_t)_instance_size;
			return gc_array_const_item_ref( _instance_size, _array_data + off, _definition );
		}
		
		gc_array_const_iter operator+( int val )
		{
			return gc_array_const_iter( *this + val, _definition, _instance_size, _array_data );
		}

		gc_array_const_iter operator-( int val )
		{
			return gc_array_const_iter( *this - val, _definition, _instance_size, _array_data );
		}
		
		gc_array_const_iter operator+( gc_array_const_iter val )
		{
			return gc_array_const_iter( *this + val, _definition, _instance_size, _array_data );
		}

		gc_array_const_iter operator-( gc_array_const_iter val )
		{
			return gc_array_const_iter( *this - val, _definition, _instance_size, _array_data );
		}

		gc_array_const_item_ref operator*() { return item_ref(); }
	};

	class gc_array_iter : public data_array_iter_base
	{
		class_definition*		_definition;
		uint32_t				_instance_size;
		uint8_t*				_array_data;
	public:

		gc_array_iter() : data_array_iter_base(), _definition( nullptr ), _instance_size( 0 ), _array_data( 0 ) {}
		gc_array_iter( const data_array_iter_base& iter_base, class_definition* def, uint32_t is, uint8_t* data )
			: data_array_iter_base( iter_base )
			, _definition( def )
			, _instance_size( is )
			, _array_data( data )
		{
		}
		gc_array_iter( const gc_array_iter& other )
			: data_array_iter_base( other )
			, _definition( other._definition )
			, _instance_size( other._instance_size )
			, _array_data( other._array_data )
		{
		}
		
		gc_array_iter& operator=( const gc_array_iter& other )
		{
			if ( this != &other )
			{
				data_array_iter_base::operator=( other );
				_definition = other._definition;
				_instance_size = other._instance_size;
				_array_data = other._array_data;
			}
			return *this;
		}

		gc_array_item_ref item_ref()
		{
			if ( index() >= (int32_t)count() 
				|| index() < 0 )
				throw runtime_error( "out of bounds array access" );
			int32_t off = index() * (int32_t)_instance_size;
			return gc_array_item_ref( _instance_size, _array_data + off, _definition );
		}

		operator gc_array_const_iter () const
		{
			return gc_array_const_iter( *this, _definition, _instance_size, _array_data );
		}
		
		gc_array_iter operator+( int val )
		{
			return gc_array_iter( data_array_iter_base::operator+( val ), _definition, _instance_size, _array_data );
		}

		gc_array_iter operator-( int val )
		{
			return gc_array_iter( data_array_iter_base::operator-(val), _definition, _instance_size, _array_data );
		}
		
		gc_array_iter operator+( gc_array_iter val )
		{
			return gc_array_iter( data_array_iter_base::operator+(val), _definition, _instance_size, _array_data );
		}

		gc_array_iter operator-( gc_array_iter val )
		{
			return gc_array_iter( data_array_iter_base::operator-(val), _definition, _instance_size, _array_data );
		}

		gc_array_item_ref operator*() { return item_ref(); }
	};
}

//You would need to use quicksort or know the exact type and use std::sort to sort the array
//there aren't facilities to generalized sort an array based off of a generic comparator unless
//its type is known.  c-quicksort requires a stand-alone function so comparators with state are 
//impossible and std::sort won't work because fundamentally the item the iterator produces (array_ref) can't
//be a reference or pointer type.

namespace cclj {

	class gc_array : public gc_obj_ptr
	{
		class_definition_ptr	_definition;
		uint8_t*				_array_data;
		uint32_t				_capacity;

		void acquire()
		{
			release();
			if ( _definition && data().first )
			{
				pair<void*,size_t> data_pair = data();
				uint8_t* data_ptr = reinterpret_cast<uint8_t*>( data_pair.first );
				int32_t data_section_length = std::max( (int32_t)0, (int32_t)data_pair.second );
				if ( data_section_length )
					_array_data = data_ptr;
				else
					_array_data = nullptr;
				
				//check that the array members line up with the data size.
				_capacity = data_section_length / _definition->instance_size();
				uint32_t leftover = (uint32_t)data_section_length % _definition->instance_size();
				if ( leftover )
					throw runtime_error( "leftover data area instance size mismatch" );
			}
		}
		void release()
		{
			_array_data = nullptr;
			_capacity = 0;
		}
		void acquire_definition( string_table_str name )
		{
			_definition = gc()->class_system()->find_definition( name );
			if ( !_definition )
				throw runtime_error( "Failed to find class definition for array" );
		}
	public:
		
		typedef gc_array_iter iterator;
		typedef gc_array_const_iter const_iterator;
		gc_array() { release(); }

		gc_array( const gc_obj_ptr& ptr )
			: gc_obj_ptr( ptr )
		{
			release();
			acquire_definition( ptr.object()->type );
			acquire();
		}

		
		gc_array( garbage_collector_ptr gc, string_table_str type )
			: gc_obj_ptr( gc )
		{
			release();
			acquire_definition( type );
		}

		gc_array( const gc_array& obj )
			: gc_obj_ptr( obj.gc() )
		{
			release();
			operator=(obj);
		}

		gc_array& operator=( const gc_array& obj )
		{
			if ( this != &obj )
			{
				release();
				//release existing data and set us up the bomb.
				gc_obj_ptr::operator=( gc_obj_ptr( gc() ) );
				_definition = obj._definition;
				if ( _definition )
				{
					//perform copy operation
					if ( obj )
					{
						reserve( obj.size() );
						memcpy( data().first, obj.data().first, obj.size() * obj.item_size() );
						object()->count = obj.size();
					}
				}
			}
			return *this;
		}

		size_t size() const { if ( !(*this) ) return 0; return object()->count; }
		size_t capacity() const { return _capacity; }
		pair<uint8_t*, size_t> array_data() { return make_pair( _array_data, size() ); }
		size_t item_size() const { if ( _definition ) return _definition->instance_size(); return 0; }
		bool empty() const { return !_definition; }

		iterator begin() 
		{
			class_definition* def = nullptr;
			if ( _definition )
				def = _definition.get();

			return iterator( data_array_iter_base( (uint32_t)size(), 0 ), def, item_size(), _array_data );
		}
		iterator end() 
		{
			class_definition* def = nullptr;
			if ( _definition )
				def = _definition.get();

			return iterator( data_array_iter_base( (uint32_t)size(), (int32_t)size() ), def, item_size(), _array_data );
		}
		
		const_iterator begin() const
		{
			return const_cast<gc_array&>( *this ).begin();
		}

		const_iterator end() const
		{
			return const_cast<gc_array&>( *this ).end();
		}

		gc_array_item_ref operator[]( int32_t idx ) 
		{ 
			if ( idx >= (int32_t)size() )
				throw runtime_error( "array access out of bounds" );
			int32_t off = idx * (int32_t)item_size();
			return gc_array_item_ref( (uint32_t)item_size(), _array_data + off, _definition.get() );
		}
		
		gc_array_const_item_ref operator[]( int32_t idx ) const
		{
			return const_cast<gc_array&>( *this ).operator[]( idx );
		}

		void reserve( size_t total )
		{
			if ( total <= capacity() ) return;
			release();
			size_t old_size = size();
			size_t new_instance_size = total * item_size();
			if ( (*this) )
				reallocate( new_instance_size, __FILE__, __LINE__  );
			else
				create( _definition->name(), new_instance_size, __FILE__, __LINE__ );
			object()->count = old_size;
			acquire();
		}

		iterator insert( iterator _where, size_t num_items )
		{
			uint32_t the_item_size = item_size();
			size_t new_size = size() + num_items;
			int32_t where_index = _where.index();
			if ( new_size > capacity() )
				reserve( new_size * 2 );
			_where = begin();
			_where += where_index;
			//move the data in the array forward from iter to when end was
			int32_t move_section_width = (end() - _where).index() * the_item_size;
			int32_t move_section_len = num_items * the_item_size;
			if ( move_section_width )
			{
				uint8_t* start_ptr = _where.item_ref()._item_data;
				uint8_t* dest_ptr = start_ptr + move_section_len;
				memmove( dest_ptr, start_ptr, move_section_width );
			}
			object()->count += num_items;
			if ( move_section_len )
			{
				//zero out new memory
				memset( (*this)[where_index]._item_data, 0, move_section_len );
			}
			return begin() + where_index;
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
			uint32_t the_item_size = item_size();
			int32_t move_section_width = (end() - stop).index() * the_item_size;
			if ( move_section_width )
			{
				uint8_t* start_ptr = stop.item_ref()._item_data;
				uint8_t* dest_ptr = start.item_ref()._item_data;
				memcpy( dest_ptr, start_ptr, move_section_width );
			}
			//No need to memset anything to zero because that will happen on insert.
			object()->count -= num_items;
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
	};
}

#endif