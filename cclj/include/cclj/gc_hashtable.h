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
		typedef gc_lock_ptr<this_type>				this_ptr_type;
		typedef pair<const key_type,value_type>		entry_type;
		typedef entry_type&							entry_ref_type;
		typedef entry_type*							entry_ptr_type;
		typedef vector<uint32_t>					uint32_list;
		class entry_node : public noncopyable
		{
		public:
			entry_type		_entry;
			entry_node*		_next_node;
			entry_node(const entry_type& entry ) 
				: _entry( entry )
				, _next_node( nullptr )
			{
			}
		};

		typedef pool<sizeof(entry_node), 100*sizeof(entry_node)> entry_pool_type;
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
			iterator( typename hash_vector_type::iterator iter, typename entry_node_list::iterator pos, typename hash_vector_type::iterator end )
				: _iter( iter )
				, _list_iter( pos )
				, _end( end )
			{
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

			typename hash_vector_type::iterator hash_iterator() const { return _iter; }
			typename entry_node_list::iterator list_iterator() const { return _list_iter; }

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
			iterator( typename hash_vector_type::const_iterator iter
					, typename entry_node_list::const_iterator pos
					, typename hash_vector_type::const_iterator end )
				: _iter( iter )
				, _list_iter( pos )
				, _end( end )
			{
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
		float					_load_factor;

		
		gc_hashtable( garbage_collector_ptr gc, file_info info
						, const key_traits_type& kt
						, const value_traits_type& vt )
			: _gc( gc )
			, _entry_pool( gc->allocator(), info, std::max( kt.alignment(), vt.alignment() ) )
			, _key_traits( kt )
			, _value_traits( vt )
			, _file_info( info )
			, _size( 0 )
			, _load_factor( .7f )
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

		void set_load_factor( float lf ) { _load_factor = lf; }

		void reserve( size_t num_items )
		{
			size_t new_bucket_count = num_items / _load_factor;
			if ( new_bucket_count > bucket_count() )
			{
				auto old_size = _hash_table.size();
				_hash_table.resize( new_bucket_count );
				//Run through and rehash all the objects.
				for ( size_t idx = 0, end = old_size; idx < end; ++idx )
				{
					entry_node* last_node = nullptr;
					entry_node* cur_node = _hash_table[idx]._head;
					while( cur_node )
					{
						size_t node_hash = _key_traits.hash( cur_node->_entry.first );
						size_t node_new_idx = node_hash % new_bucket_count;
						//If the node should be moved.
						if ( node_new_idx != idx )
						{
							entry_node* remove_node = cur_node;
							//remove from this list.
							if ( last_node != nullptr )
							{
								last_node->_next_node = cur_node->_next_node;
							}
							else
							{
								_hash_table[idx]._head = cur_node->_next_node;
							}
							cur_node = cur_node->_next_node;
							//This cannot overwrite the current pos because we check that node_new_idx != idx;
							_hash_table[node_new_idx].push_front( *remove_node );
						}
						else
						{
							last_node = cur_node;
							cur_node = cur_node->_next_node;
						}
					}
				}
			}
		}

		
		pair<typename hash_vector_type::const_iterator, typename entry_node_list::const_iterator> internal_find_const( const key_type& entry, size_t key_hash ) const
		{
			typedef pair<typename hash_vector_type::const_iterator, typename entry_node_list::const_iterator> return_type;
			if ( _hash_table.empty() )
				return return_type( _hash_table.end(), typename entry_node_list::const_iterator() );

			size_t entry_idx = key_hash % _hash_table.size();
			entry_node_list& entry_list = _hash_table[entry_idx];
			if ( entry_list.empty() )
				return return_type( _hash_table.end(), typename entry_node_list::const_iterator() );


			for ( auto iter = entry_list.begin(), end = entry_list.end(); iter != end; ++iter )
			{
				if ( _key_traits.equal( iter->m_entry.first, entry ) )
					return return_type( _hash_table.begin() + entry_idx, iter );
			}

			return return_type( _hash_table.end(), typename entry_node_list::const_iterator() );
		}

		pair<typename hash_vector_type::iterator, typename entry_node_list::iterator> internal_find( const key_type& entry, size_t key_hash )
		{
			typedef pair<typename hash_vector_type::iterator, typename entry_node_list::iterator> return_type;
			auto internal_const_result = internal_find_const( entry, key_hash );
			if ( internal_const_result.first != _hash_table.end() )
				return return_type( internal_const_result.first - _hash_table.begin(), const_cast<entry_node*>( &(*internal_const_result.second ) ) );

			return return_type( _hash_table.end(), typename entry_node_list::iterator() );
		}

		pair<iterator,bool> insert( const pair<key_type, value_type>& entry )
		{
			size_t key_hash = _key_traits.hash( entry.first );
			auto finder = internal_find( entry.first, key_hash );
			bool exists = finder.first != _hash_table.end() 
				&& finder.second != finder.first->end();

			if ( exists )
				return pair<iterator,bool>( iterator( finder.first, finder.second, _hash_table.end() ), false );

			float new_load_factor = 1;
			++_size;
			if ( bucket_count() )
				new_load_factory = (float)size() / (float)bucket_count();

			if ( new_load_factor > _load_factor )
			{
				reserve( std::max( 16, bucket_count() * 2 ) );
			}
			size_t entry_idx = key_hash % _hash_table.size();
			entry_node* new_node = _pool.construct<entry_node>( entry_type( entry ) );
			_hash_table[entry_idx].push_front( new_node );
			return pair<iterator,bool>( iterator( _hash_table.begin() + entry_idx, new_node, _hash_table.end() ), true );
		}

		void remove_node( typename hash_vector_type::iterator item, entry_node& entry_node )
		{
			item->remove( &entry_node );
			--size;
			_pool.destruct( &entry_node );
		}

		bool erase( const key_type& entry )
		{
			auto finder = internal_find( entry.first, key_hash );
			if ( finder.first != _hash_table.end() )
			{
				remove_node( finder.first, &(*finder.second) );
				return true;
			}
			return false;
		}

		bool erase( iterator entry )
		{
			auto hash_iter = entry.hash_iterator();
			auto list_iter = entry.list_iterator();
			if ( hash_iter != _hash_table.end() )
			{
				remove_node( hash_iter, *list_iter );
				return true;
			}
			return false;
		}

		bool contains( const key_type& k ) const
		{ 
			auto finder = internal_find_const( k, key_traits.hash( key ) );
			return finder.first != _hash_table.end()
				&& finder.second != finder.first->end();
		}

		iterator find( const key_type& k )
		{
			auto finder = internal_find( k, key_traits.hash( key ) );
			if ( finder.first != _hash_table.end()
				&& finder.second != finder.first->end() )
				return iterator( finder.first, finder.second, _hash_table.end() );
		}
		
		const_iterator find( const key_type& k ) const
		{
			auto finder = internal_find_const( k, key_traits.hash( key ) );
			if ( finder.first != _hash_table.end()
				&& finder.second != finder.first->end() )
				return const_iterator( finder.first, finder.second, _hash_table.end() );
		}

		const_iterator begin() const
		{
			if ( size() == 0 ) return end();
			for ( size_t idx = 0, end = _hash_table.size(); idx < end; ++idx )
			{
				if ( !_hash_table[idx].empty() )
					return const_iterator( _hash_table.begin() + idx, _hash_table[idx].begin(), _hash_table.end() );
			}
			return end();
		}

		iterator begin()
		{
			if ( size() == 0 ) return end();
			for ( size_t idx = 0, end = _hash_table.size(); idx < end; ++idx )
			{
				if ( !_hash_table[idx].empty() )
					return iterator( _hash_table.begin() + idx, _hash_table[idx].begin(), _hash_table.end() );
			}
			return end();
		}

		iterator end()
		{
			return iterator( _hash_table.end(), nullptr, _hash_table.end() );
		}

		const_iterator end() const
		{
			return const_iterator( _hash_table.end(), nullptr, _hash_table.end() );
		}

		size_t size() const { return _size; }
		size_t bucket_count() const { return _hash_table.size(); }

	};
}

#endif