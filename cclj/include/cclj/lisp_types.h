//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_LISP_TYPES_H
#define CCLJ_LISP_TYPES_H
#pragma once
#include "cclj/cclj.h"
#include "cclj/noncopyable.h"
#include "cclj/type_library.h"
#include "cclj/allocator.h"
#include "cclj/invasive_list.h"

namespace cclj { namespace lisp {
	struct types
		{
			enum _enum
			{
				unknown_type = 0,
				cons_cell,
				symbol,
				array,
				constant,
			};
		};


		class object: public noncopyable
		{
		protected:
		public:
			virtual ~object(){}
			virtual types::_enum type() const = 0;
		};


		typedef object* object_ptr;
		typedef data_buffer<object_ptr> object_ptr_buffer;


		class cons_cell : public object
		{
		public:
			object* _value;
			object* _next;
			cons_cell()
				: _value( nullptr )
				, _next( nullptr )
			{
			}
			enum { item_type = types::cons_cell };
			virtual types::_enum type() const { return types::cons_cell; }
		};

		CCLJ_DEFINE_INVASIVE_SINGLE_LIST(cons_cell);
		


		class array : public object
		{
		public:
			object_ptr_buffer _data;
			enum { item_type = types::array };
			virtual types::_enum type() const { return types::array; }
		};


		class symbol : public object
		{
		public:
			string_table_str	_name;
			type_ref*			_type;
			symbol() : _type( nullptr ) {}

			enum { item_type = types::symbol };
			virtual types::_enum type() const { return types::symbol; }
		};


		class constant : public object
		{
		public:
			type_ref*		_type;
			uint8_t*		_value; 
			constant() : _type( nullptr ), _value( nullptr ) {}

			enum { item_type = types::constant };
			virtual types::_enum type() const { return types::constant; }
		};

		class object_traits
		{
		public:
			template<typename obj_type>
			static obj_type* cast( object* obj )
			{
				if ( obj == nullptr ) return nullptr;
				if ( obj->type() == obj_type::item_type ) return static_cast<obj_type*>( obj );
				return nullptr;
			}

			template<typename obj_type>
			static obj_type& cast( object& obj )
			{
				if ( obj.type() == obj_type::item_type ) return static_cast<obj_type&>( obj );
				throw runtime_error( "invalid cast" );
			}

			template<typename obj_type>
			static obj_type& cast_ref( object* obj )
			{
				if ( obj->type() == obj_type::item_type ) return *static_cast<obj_type*>( obj );
				throw runtime_error( "invalid cast" );
			}
		};

		class factory
		{
		protected:
			virtual ~factory(){}
		public:
			friend class shared_ptr<factory>;
			virtual const cons_cell& empty_cell() = 0;
			virtual cons_cell* create_cell() = 0;
			virtual array* create_array() = 0;
			virtual symbol* create_symbol() = 0;
			virtual constant* create_constant() = 0;
			virtual uint8_t* allocate_data( size_t size, uint8_t alignment ) = 0;
			virtual object_ptr_buffer allocate_obj_buffer(size_t size) = 0;

			static shared_ptr<factory> create_factory( allocator_ptr allocator, const cons_cell& empty_cell );
		};

		typedef shared_ptr<factory> factory_ptr;

		
		inline cons_cell* cons_cell_next_op::get( cons_cell& cell ) { return object_traits::cast<cons_cell>( cell._next ); }
		inline const cons_cell* cons_cell_next_op::get( const cons_cell& cell ) const 
		{ 
			return object_traits::cast<cons_cell>( const_cast<object*>( cell._next ) );
		}
		inline void cons_cell_next_op::set( cons_cell& cell, cons_cell* next ) { cell._next = next; }
}}

#endif