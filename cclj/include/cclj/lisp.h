//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_LISP_H
#define CCLJ_LISP_H
#pragma once

#include "cclj/cclj.h"
#include "cclj/string_table.h"
#include "cclj/data_buffer.h"
#include "cclj/noncopyable.h"
#include "cclj/allocator.h"

namespace cclj
{
	namespace lisp
	{

		struct types
		{
			enum _enum
			{
				unknown_type = 0,
				cons_cell,
				symbol,
				array,
				constant,
				type_ref,
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


		class array : public object
		{
		public:
			object_ptr_buffer _data;
			enum { item_type = types::array };
			virtual types::_enum type() const { return types::array; }
		};

		
		class type_ref : public object
		{
		public:
			string_table_str			_name;
			data_buffer<object_ptr>		_specializations;
			enum { item_type = types::type_ref };
			virtual types::_enum type() const { return types::type_ref; }
		};




		class symbol : public object
		{
		public:
			string_table_str	_name;
			type_ref*			_type;

			enum { item_type = types::symbol };
			virtual types::_enum type() const { return types::symbol; }
		};


		class constant : public object
		{
		public:
			float value; //for now.  Really this should be an unparsed string so we can late-convert the value
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
				if ( obj->type() == obj_type::type ) return static_cast<obj_type*>( obj );
				return nullptr;
			}

			template<typename obj_type>
			static obj_type& cast( object& obj )
			{
				if ( obj->type() == obj_type::type ) return static_cast<obj_type&>( obj );
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
			virtual type_ref* create_type_ref() = 0;
			virtual object_ptr_buffer allocate_obj_buffer(size_t size) = 0;

			static shared_ptr<factory> create_factory( allocator_ptr allocator, const cons_cell& empty_cell );
		};

		typedef shared_ptr<factory> factory_ptr;

		class reader
		{
		public:
			
			friend class shared_ptr<reader>;
			//Probably need some erro reporting in there somewhere.
			virtual object_ptr_buffer parse( const string& str );

			static shared_ptr<reader> create_reader( factory_ptr factory, string_table_ptr str_table );
		};

	}
}

#endif