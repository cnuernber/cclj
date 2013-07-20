//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_INVASIVE_LIST_H
#define CCLJ_INVASIVE_LIST_H
#pragma once

#include "cclj/cclj.h"

namespace cclj
{
		//Base linked list without an included head or tail member.
	template<typename obj_type, 
			typename obj_head_op,
			typename obj_tail_op>
	class invasive_list_base
	{
	public:
		obj_type* tail( obj_type* inObj )
		{
			if ( inObj )
				return obj_tail_op().get( inObj );
			return nullptr;
		}

		obj_type* head( obj_type* inObj )
		{
			if ( inObj )
				return obj_head_op().get( inObj );
			return nullptr;
		}
		
		const obj_type* tail( const obj_type* inObj )
		{
			if ( inObj )
				return obj_tail_op().get( inObj );
			return nullptr;
		}

		const obj_type* head( const obj_type* inObj )
		{
			if ( inObj )
				return obj_head_op().get( inObj );
			return nullptr;
		}

		void remove( obj_type& inObj )
		{
			obj_head_op theHeadOp;
			obj_tail_op theTailOp;
			obj_type* theHead = theHeadOp.get( inObj );
			obj_type* theTail = theTailOp.get( inObj );
			if ( theHead )
				theTailOp.set( *theHead, theTail );
			if ( theTail )
				theHeadOp.set( *theTail, theHead );
			theHeadOp.set( inObj, nullptr );
			theTailOp.set( inObj, nullptr );
		}

		void insert_after( obj_type& inPosition, obj_type& inObj )
		{
			obj_tail_op theTailOp;
			obj_type* theHead = &inPosition;
			obj_type* theTail = theTailOp.get( inPosition );
			insert( theHead, theTail, inObj );
		}
		
		void insert_before( obj_type& inPosition, obj_type& inObj )
		{
			obj_head_op theHeadOp;
			obj_type* theHead = theHeadOp.get( inPosition );
			obj_type* theTail = &inPosition;
			insert( theHead, theTail, inObj );
		}

		void insert( obj_type* inHead, obj_type* inTail, obj_type& inObj )
		{
			obj_head_op theHeadOp;
			obj_tail_op theTailOp;
			if ( inHead )
				theTailOp.set(*inHead, &inObj);
			if ( inTail )
				theHeadOp.set(*inTail, &inObj);
			theHeadOp.set(inObj, inHead);
			theTailOp.set(inObj, inTail);
		}
	};
	
	template<typename obj_type,
			typename obj_tail_op>
	class invasive_list_iterator
	{
	public:
		typedef invasive_list_iterator<obj_type,obj_tail_op> this_type;
		obj_type* m_Obj;
		invasive_list_iterator(obj_type* inObj = nullptr) : m_Obj( inObj ) {}

		bool operator != (const this_type& inIter ) const { return m_Obj != inIter.m_Obj; }
		bool operator == (const this_type& inIter ) const { return m_Obj == inIter.m_Obj; }

		this_type& operator++()
		{
			if ( m_Obj )
				m_Obj = obj_tail_op().get( *m_Obj );
			return *this;
		}
		
		this_type& operator++(int)
		{
			this_type retval( *this );
			++(*this);
			return retval;
		}

		obj_type& operator*() { return *m_Obj; }
		obj_type* operator->() { return m_Obj; }
	};


	//Used for singly linked list where
	//items have either no head or tail ptr.
	template<typename obj_type>
	class invasive_list_null_op
	{
	public:
		void set( obj_type&, obj_type* ) {}
		obj_type* get( const obj_type&) { return nullptr; }
	};

	template<typename obj_type,
			typename obj_tail_op>
	class invasive_single_linked_list : public invasive_list_base<obj_type, invasive_list_null_op<obj_type>, obj_tail_op>
	{
	public:
		typedef invasive_single_linked_list<obj_type,obj_tail_op> this_type;
		typedef invasive_list_base<obj_type, invasive_list_null_op<obj_type>, obj_tail_op> base_type;
		typedef invasive_list_iterator<obj_type,obj_tail_op> iterator;
		typedef iterator const_iterator;
		obj_type* _head;
		invasive_single_linked_list()
			: _head( nullptr )
		{
		}
		invasive_single_linked_list( const this_type& inOther )
			: _head( inOther._head )
		{}
		this_type& operator=( const this_type& inOther )
		{
			_head = inOther._head;
			return *this;
		}


		obj_type& front() const { return *_head; }

		void push_front( obj_type& inObj )
		{
			if ( _head != nullptr )
				base_type::insert_before( *_head, inObj );
			_head = &inObj;
		}

		void push_back( obj_type& inObj )
		{
			if ( _head == nullptr )
				_head = &inObj;
			else
			{
				obj_type* lastObj = nullptr;
				for ( iterator iter = begin(), endIter = end(); iter != endIter; ++iter )
					lastObj = &(*iter);

				NV_ASSERT( lastObj );
				if ( lastObj )
					obj_tail_op().set( *lastObj, &inObj );
			}
		}

		void remove( obj_type& inObj )
		{
			if ( _head == &inObj )
				_head = obj_tail_op().get( inObj );
			base_type::remove( inObj );
		}

		bool empty() const { return _head == nullptr; }

		iterator begin() { return iterator( _head ); }
		iterator end() { return iterator( nullptr ); }

		const_iterator begin() const { return iterator( _head ); }
		const_iterator end() const { return iterator( nullptr ); }
	};

	template<typename obj_type,
			typename obj_head_op,
			typename obj_tail_op>
	class invasive_linked_list : public invasive_list_base<obj_type, obj_head_op, obj_tail_op>
	{
	public:
		typedef invasive_linked_list<obj_type,obj_head_op,obj_tail_op> this_type;
		typedef invasive_list_base<obj_type, obj_head_op, obj_tail_op> base_type;
		typedef invasive_list_iterator<obj_type,obj_tail_op> iterator;
		typedef iterator const_iterator;
		typedef invasive_list_iterator<obj_type,obj_head_op> reverse_iterator;
		typedef reverse_iterator const_reverse_iterator;

		obj_type* _head;
		obj_type* _tail;

		invasive_linked_list() : _head( nullptr ), _tail( nullptr ) {}
		invasive_linked_list(const this_type& inOther) : _head( inOther._head ), _tail( inOther._tail ) {}
		this_type& operator=(const this_type& inOther) 
		{
			_head = inOther._head;
			_tail = inOther._tail;
			return *this;
		}

		obj_type& front() const { NV_ASSERT( _head ); return *_head; }
		obj_type& back() const { NV_ASSERT( _tail ); return *_tail; }

		obj_type* front_ptr() const { return _head; }
		obj_type* back_ptr() const { return _tail; }

		void push_front( obj_type& inObj )
		{
			if ( _head != nullptr )
				base_type::insert_before( *_head, inObj );
			_head = &inObj;

			if ( _tail == nullptr )
				_tail = &inObj;
		}

		void push_back( obj_type& inObj )
		{
			if ( _tail != nullptr )
				base_type::insert_after( *_tail, inObj );
			_tail = &inObj;

			if ( _head == nullptr )
				_head = &inObj;
		}

		void remove( obj_type& inObj )
		{
			if ( _head == &inObj )
				_head = obj_tail_op().get( inObj );
			if ( _tail == &inObj )
				_tail = obj_head_op().get( inObj );

			base_type::remove( inObj );
		}

		bool empty() const { return _head == nullptr; }
		
		iterator begin() { return iterator( _head ); }
		iterator end() { return iterator( nullptr ); }

		const_iterator begin() const { return iterator( _head ); }
		const_iterator end() const { return iterator( nullptr ); }
		
		reverse_iterator rbegin() { return reverse_iterator( _tail ); }
		reverse_iterator rend() { return reverse_iterator( nullptr ); }

		const_reverse_iterator rbegin() const { return reverse_iterator( _tail ); }
		const_reverse_iterator rend() const { return reverse_iterator( nullptr ); }
	};
			
	
	//Macros to speed up the definitely of invasive linked lists.
#define CCLJ_DEFINE_INVASIVE_SINGLE_LIST( type )								\
	class type;																	\
	class type##_next_op														\
	{																			\
	public:																		\
		type* get( type& s );													\
		const type* get( const type& s ) const;									\
		void set( type& inItem, type* inNext );									\
	};																			\
	typedef invasive_single_linked_list<type,type##_next_op> type##_list;	

	
#define CCLJ_DEFINE_INVASIVE_LIST( type )													\
	class type;																				\
	class type##_next_op																	\
	{																						\
	public:																					\
		type* get( type& s );																\
		const type* get( const type& s ) const;												\
		void set( type& inItem, type* inNext );												\
	};																						\
	class type##_previous_op																\
	{																						\
		type* get( type& s );																\
		const type* get( const type& s ) const;												\
		void set( type& inItem, type* inNext );												\
	};																						\
	typedef invasive_linked_list<type,type##previous_op,type##next_op> type##_list;

		
#define CCLJ_IMPLEMENT_INVASIVE_LIST( type, prevMember, nextMember )									\
	inline type* type##_next_op::get( type& s ) { return s.nextMember; }									\
	inline const type* type##_next_op::get( const type& s ) const { return s.nextMember; }				\
	inline void type##_next_op::set( type& inItem, type* inNext ) { inItem.nextMember = inNext; }		\
	inline type* type##_previous_op::get( type& s ) { return s.prevMember; }								\
	inline const type* type##_previous_op::get( const type& s ) const { return s.prevMember; }			\
	inline void type##_previous_op::set( type& inItem, type* inNext ) { inItem.prevMember = inNext; }	\
	
#define CCLJ_IMPLEMENT_INVASIVE_SINGLE_LIST( type, nextMember )											\
	inline type* type##_next_op::get( type& s ) { return s.nextMember; }									\
	inline const type* type##_next_op::get( const type& s ) const { return s.nextMember; }				\
	inline void type##_next_op::set( type& inItem, type* inNext ) { inItem.nextMember = inNext; }
}

#endif