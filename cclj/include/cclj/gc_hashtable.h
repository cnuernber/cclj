//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_GC_HASHTABLE_H
#define CCLJ_GC_HASHTABLE_H
#pragma once
#include "cclj/garbage_collector.h"
#include "cclj/algo_util.h"
#include "cclj/object_traits.h"
#include "cclj/pool.h"
#include "cclj/gc_array.h"
#include "cclj/invasive_list.h"

namespace cclj
{
	template<typename obj_type>
	class gc_hashtable_traits : public default_object_traits<obj_type>, public gc_static_traits<obj_type>
	{
	public:
		static size_t hash(const obj_type& obj) { return 0; }
		static bool equals(const obj_type& lhs, const obj_type& rhs ) { return lhs == rhs; }
	};
	
	//naive hashtable until it is proven this won't work.
	template<typename key_type, typename value_type
				, typename key_traits_type = gc_hashtable_traits<key_type>
				, typename value_traits_type = gc_array_traits<value_type> >
	class gc_hashtable : public gc_object
	{
	public:
		typedef gc_hashtable<key_type, value_type, key_traits_type, value_traits_type> this_type;
		typedef gc_lock_ptr<this_type>		this_ptr_type;
		typedef pair<key_type,value_type>	entry_type;
		typedef entry_type&					entry_ref_type;
		typedef entry_type*					entry_ptr_type;
		typedef vector<uint32_t>			uint32_list;
		class entry_node : public noncopyable
		{
		public:
			entry_type		_entry;
			entry_node*		_next_node;
			entry_node(const key_type& key, const value_type& value ) 
				: _entry( key, value )
				, _next_node( nullptr )
			{
			}
		};

		typedef pool<sizeof(entry_node)>		entry_pool_type;
		struct entry_node_next_op
		{
			entry_node* get( entry_node& node ) { return node._next_node; }
			void set( entry_node& node, entry_node* next ) { node._next_node = next; }
		};
		typedef invasive_single_linked_list<entry_node, entry_node_next_op> entry_node_list;
		typedef vector<entry_node_list>			hash_vector_type;

		class iterator
		{
			typename hash_vector_type::iterator	_iter;
			typename entry_node_list::iterator	_list_iter;
			typename hash_vector_type::iterator	_end;
		public:
			iterator( typename hash_vector_type::iterator iter, typename hash_vector_type::iterator end )
				: _iter( iter )
				, _end( end )
			{
				if ( _iter != _end )
					_list_iter = _iter->begin();
			}
			iterator( const iterator& other )
				: _iter( other._iter )
				, _list_iter( other._list_iter )
			{
			}
			iterator& operator=( const iterator& other )
			{
				_iter = other._iter;
				_list_iter = other._list_iter;
				return *this;
			}
			entry_type& operator*() { return _list_iter->_entry; }
			entry_type* operator->() { return &_list_iter->_entry; }
			template<typename vec_iter, typename list_iter>
			static bool next_valid_item( vec_iter& _iter, list_iter& _list_iter, vec_iter& _end )
			{
				if ( _iter != _end )
				{
					if ( _list_iter != _iter->end() )
						++_list_iter;
					if ( _list_iter != _iter->end() )
						return true;
				}
				++_iter;
				for ( ; _iter != _end; ++_iter )
				{
					if ( _iter->empty() == false )
					{
						_list_iter = _iter->begin();
						return true;
					}
				}
				return false;
			}
			iterator& operator++()
			{
				next_valid_item( _iter, _list_iter, _end );
				return *this;
			}
			iterator operator++( int )
			{
				iterator retval( *this );
				++(*this);
				return retval;
			}

			bool operator==( const iterator& other ) const
			{
				return _list_iter == other._list_iter
					&& _iter == other._iter;
			}
			
			bool operator!=( const iterator& other ) const
			{
				return _list_iter != other._list_iter
					|| _iter == other._iter;
			}
		};
		
		class const_iterator
		{
			typename hash_vector_type::const_iterator	_iter;
			typename entry_node_list::const_iterator	_list_iter;
			typename hash_vector_type::const_iterator	_end;
		public:
			const_iterator( typename hash_vector_type::const_iterator iter
							, typename hash_vector_type::const_iterator end )
				: _iter( iter )
				, _end( end )
			{
				if ( _iter != _end )
					_list_iter = _iter->begin();
			}
			const_iterator( const const_iterator& other )
				: _iter( other._iter )
				, _list_iter( other._list_iter )
			{
			}
			const_iterator& operator=( const const_iterator& other )
			{
				_iter = other._iter;
				_list_iter = other._list_iter;
				return *this;
			}
			const entry_type& operator*() { return _list_iter->_entry; }
			const entry_type* operator->() { return &_list_iter->_entry; }
			const_iterator& operator++()
			{
				iterator::next_valid_item( _iter, _list_iter, _end );
				return *this;
			}
			const_iterator operator++( int )
			{
				const_iterator retval( *this );
				++(*this);
				return retval;
			}

			bool operator==( const const_iterator& other ) const
			{
				return _list_iter == other._list_iter
					&& _iter == other._iter;
			}
			
			bool operator!=( const const_iterator& other ) const
			{
				return _list_iter != other._list_iter
					|| _iter == other._iter;
			}
		};
	private:

		garbage_collector_ptr	_gc;
		entry_pool_type			_entry_pool;
		hash_vector_type		_hash_table;
		key_traits_type			_key_traits;
		value_traits_type		_value_traits;
		file_info				_file_info;
		size_t					_size;

		
		gc_hashtable( garbage_collector_ptr gc, file_info info
						, const key_traits_type& kt
						, const value_traits_type& vt )
			: _gc( gc )
			, _entry_pool( gc->allocator(), info, std::max( kt.alignment(), vt.alignment() ) )
			, _key_traits( kt )
			, _value_traits( vt )
			, _file_info( info )
			, _size( 0 )
		{
			initialize( gc );
		}
		
	public:
		
		static object_constructor create_constructor(garbage_collector_ptr gc
														, file_info location
														, const key_traits_type& kt
														, const value_traits_type& vt )
		{
			return [=]( uint8_t* mem, size_t)
			{
				this_type* data = new (mem) this_type(gc, location, kt, vt);
				return data;
			};
		}

		static this_ptr_type create( garbage_collector_ptr gc, file_info location
														, const key_traits_type& kt = key_traits_type()
														, const value_traits_type& vt = value_traits_type() )
		{
			gc_object& retval = gc->allocate_object( sizeof( this_type )
									, sizeof(void*), create_constructor( gc, location, kt, vt )
									, location );
			return this_ptr_type( gc, reinterpret_cast<this_type&>( retval ) );
		}
		

	};
}

#endif