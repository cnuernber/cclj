//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_LANGUAGE_TYPES_H
#define CCLJ_LANGUAGE_TYPES_H
#include "cclj/cclj.h"
#include "cclj/gc.h"
#include "cclj/string_table.h"

namespace cclj
{
	struct type_ids
	{
		enum val
		{
			no_type_id	=			0,
			cons_cell	=			1 << 0,
			number		=			1 << 1, 
			symbol		=			1 << 2,
			function	=			1 << 3,
			context		=			1 << 4,
		};
	};

	//Anything that fits in 32 bits doesn't require locking and won't ever be moved
	//by the garbage collector.
	typedef float cclj_number;
	cclj_number number_from_gc_object( gc_object& val ) { return *reinterpret_cast<float*>( &val.user_data ); }
	void number_to_gc_object( gc_object& val, cclj_number num ) { val.user_data = *reinterpret_cast<uint32_t*>( &num ); }

	template<typename lang_type>
	lang_type* lang_type_cast( gc_object& val, garbage_collector& gc ) 
	{
		if ( val.user_flags == lang_type::cclj_type )
		{
			pair<void*,size_t> gc_data = gc.lock( val );
			return reinterpret_cast<lang_type*>( gc_data.first );
		}
		return nullptr;
	};
	

	template<typename lang_type>
	struct cclj_gc_ptr
	{
		garbage_collector&	gc;
		gc_object*			val;
		lang_type*			type;

		cclj_gc_ptr( garbage_collector& _gc, gc_object& _val )
			: gc( _gc ), val( &_val )
		{
			type = lang_type_cast<lang_type>( *val, gc );
		}
		~cclj_gc_ptr()
		{
			release_type();
		}

		cclj_gc_ptr( const cclj_gc_ptr<lang_type>& other )
			: gc( other.gc ), val( other.val ), type( other.type )
		{
			if ( type ) gc.lock( *val );
		}
		
		cclj_gc_ptr<lang_type>& operator=( const cclj_gc_ptr<lang_type>& other )
		{
			if ( type != other.type )
			{
				if ( type ) gc.unlock( val );
				type = other.type;
				val = other.val;
				if ( type ) gc.lock( val );
			}
			return *this;
		}

		bool operator==( const cclj_gc_ptr<lang_type>& other ) const
		{
			return type == other.type;
		}
		
		bool operator!=( const cclj_gc_ptr<lang_type>& other ) const
		{
			return type != other.type;
		}

		operator bool() const { return type != nullptr; }

		void release_type()
		{
			if ( type ) gc.unlock( *val );
			type = nullptr;
		}

		lang_type* operator->() const { assert(type); return type; }
		lang_type* operator*() const { assert(type); return type; }
	};

	struct lang_type_creator
	{
		template<typename lang_type>
		static cclj_gc_ptr<lang_type> do_create_lang_type( garbage_collector& gc, const char* file, int line )
		{
			gc_object& obj = gc.allocate( sizeof( lang_type ), file, line );
			obj.user_flags = lang_type::cclj_type;
			cclj_gc_ptr<lang_type> retval( gc, obj );
			new (retval.type) lang_type();
			return retval;
		}
	};

	class cclj_noncopyable
	{
	private:
		
		cclj_noncopyable( const cclj_noncopyable& );
		cclj_noncopyable& operator=( const cclj_noncopyable& );
	public:
		cclj_noncopyable(){}
	};

	class cons_cell : cclj_noncopyable
	{
		gc_object* value;
		gc_object* next;
		cons_cell() : value( nullptr ), next( nullptr ) {}


	public:
		enum { cclj_type = type_ids::cons_cell };
		friend struct lang_type_creator;

		gc_object* get_next() const { return next; }
		gc_object* get_value() const { return value; }
		void set_next( gc_object* item ) { next = item; }
		void set_value( gc_object* item ) { value = item; }

		static cclj_gc_ptr<cons_cell> create( garbage_collector& gc, const char* file, int line )
		{
			return lang_type_creator::do_create_lang_type<cons_cell>( gc, file, line );
		}
	};

	class symbol : cclj_noncopyable
	{
		string_table_str ns;
		string_table_str name;

		symbol() {}
	public:
		
		enum { cclj_type = type_ids::symbol };
		friend struct lang_type_creator;
		
		string_table_str get_ns() const { return ns; }
		string_table_str get_name() const { return name; }
		void set_ns( string_table_str item ) { ns = item; }
		void set_name( string_table_str item ) { name = item; }
		
		
		static cclj_gc_ptr<symbol> create( garbage_collector& gc, const char* file, int line )
		{
			return lang_type_creator::do_create_lang_type<symbol>( gc, file, line );
		}
	};

	class function : cclj_noncopyable
	{
		gc_object* context;
		gc_object* body; //had better be a cons cell

		function() {}
	public:
		enum { cclj_type = type_ids::function };
		friend struct lang_type_creator;
		
		gc_object* get_context() const { return context; }
		gc_object* get_body() const { return body; }
		void set_ns( gc_object* item ) { context = item; }
		void set_name( gc_object* item ) { body = item; }

		
		static cclj_gc_ptr<function> create( garbage_collector& gc, const char* file, int line )
		{
			return lang_type_creator::do_create_lang_type<function>( gc, file, line );
		}

	};

	typedef unordered_map<string_table_str,gc_object*> string_object_map;

	class context : cclj_noncopyable
	{
		gc_object*			parent_context;
		string_object_map	values;

		context() : parent_context( nullptr ) {}

	public:
		enum { cclj_type = type_ids::context };
		friend struct lang_type_creator;
		
		gc_object*			get_parent_context() const	{ return parent_context; }
		string_object_map&	get_values()				{ return values; }
		void set_parent_context( gc_object* item )		{ parent_context = item; }
		
		static cclj_gc_ptr<context> create( garbage_collector& gc, const char* file, int line )
		{
			return lang_type_creator::do_create_lang_type<context>( gc, file, line );
		}
	};
}
#endif