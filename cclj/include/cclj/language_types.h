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
			no_type_id		=			0,
			cons_cell		=			1 << 0,
			number			=			1 << 1, 
			symbol			=			1 << 2,
			function		=			1 << 3,
			context			=			1 << 4,
			user_function	=			1 << 5,
		};
	};

	//Anything that fits in 32 bits doesn't require locking and won't ever be moved
	//by the garbage collector.
	typedef float cclj_number;
	inline cclj_number number_from_gc_object( gc_object& val ) { return *reinterpret_cast<float*>( &val.user_data ); }
	inline void number_to_gc_object( gc_object& val, cclj_number num ) { val.user_data = *reinterpret_cast<uint32_t*>( &num ); }

	template<typename lang_type>
	inline lang_type* lang_type_cast( gc_object& val, garbage_collector_ptr& gc ) 
	{
		if ( gc && val.user_flags == lang_type::cclj_type )
		{
			pair<void*,size_t> gc_data = gc.lock( val );
			return reinterpret_cast<lang_type*>( gc_data.first );
		}
		return nullptr;
	};
	

	template<typename lang_type>
	class lang_type_ptr
	{
		gc_obj_ptr			_gc_object;
		lang_type*			_lang_type;
		void acquire()
		{
			_lang_type = nullptr;
			if ( _gc_object && _gc_object->user_flags == lang_type::cclj_type )
				_lang_type = reinterpret_cast<lang_type*>( _gc_object.data().first );
		}
		void release()
		{
			_lang_type = nullptr;
		}
	public:
		
		lang_type_ptr() : _lang_type( nullptr ) {}
		lang_type_ptr( const gc_obj_ptr& _val )
			: _gc_object( _val ), _lang_type( nullptr )
		{
			acquire();
		}
		~lang_type_ptr()
		{
			release();
		}

		lang_type_ptr( const lang_type_ptr<lang_type>& other )
			: _gc_object( other._gc_object ), _lang_type( nullptr )
		{
			acquire();
		}
		
		lang_type_ptr<lang_type>& operator=( const lang_type_ptr<lang_type>& other )
		{
			if ( _lang_type != other._lang_type )
			{
				release();
				_gc_object = other._gc_object;
				acquire();
			}
			return *this;
		}

		bool operator==( const lang_type_ptr<lang_type>& other ) const
		{
			return _lang_type == other._lang_type;
		}
		
		bool operator!=( const lang_type_ptr<lang_type>& other ) const
		{
			return _lang_type != other._lang_type;
		}

		operator bool () const { return _lang_type != nullptr; }

		lang_type* operator->() const { assert(_lang_type); return _lang_type; }
		lang_type& operator*() const { assert(_lang_type); return *_lang_type; }

		gc_obj_ptr obj() const { return _gc_object; }

		gc_object* object() const { return _gc_object.object(); }
		garbage_collector_ptr gc() const { return _gc_object.gc(); }
	};

	struct lang_type_creator
	{
		template<typename lang_type>
		static lang_type_ptr<lang_type> do_create_lang_type( garbage_collector_ptr gc, const char* file, int line )
		{
			gc_object& obj = gc->allocate( sizeof( lang_type ), file, line );
			obj.user_flags = lang_type::cclj_type;
			lang_type_ptr<lang_type> retval( gc_obj_ptr( gc, obj ) );
			lang_type& newType( *retval );
			new (&newType) lang_type();
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

		static lang_type_ptr<cons_cell> create( garbage_collector_ptr gc, const char* file, int line )
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
		
		
		static lang_type_ptr<symbol> create( garbage_collector_ptr gc, const char* file, int line )
		{
			return lang_type_creator::do_create_lang_type<symbol>( gc, file, line );
		}
	};

	class fn : cclj_noncopyable
	{
		gc_object* context;
		gc_object* body; //had better be a cons cell.

		fn() {}
	public:
		enum { cclj_type = type_ids::function };
		friend struct lang_type_creator;
		
		gc_object* get_context() const { return context; }
		gc_object* get_body() const { return body; }
		void set_context( gc_object* item ) { context = item; }
		void set_body( gc_object* item ) { body = item; }

		
		static lang_type_ptr<fn> create( garbage_collector_ptr gc, const char* file, int line )
		{
			return lang_type_creator::do_create_lang_type<fn>( gc, file, line );
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
		
		static lang_type_ptr<context> create( garbage_collector_ptr gc, const char* file, int line )
		{
			return lang_type_creator::do_create_lang_type<context>( gc, file, line );
		}
	};

	
	template<typename TDataType>
	class data_buffer
	{
		TDataType*	_buffer;
		size_t		_size;
	public:
		data_buffer( TDataType* bufData, size_t size )
			: _buffer( bufData )
			, _size( size )
		{
		}
		data_buffer( vector<TDataType>& buf )
			: _buffer( nullptr )
			, _size( buf.size() )
		{
			if ( buf.size() ) _buffer = &(buf[0]);
		}
		data_buffer() : _buffer( nullptr ), _size( 0 ) {}
		size_t size() const { return _size; }
		TDataType* begin() const { return _buffer; }
		TDataType* end() const { return _buffer + size(); }
		TDataType& operator[]( int idx ) const { assert( idx < size ); return _buffer[idx]; }
	};

	typedef data_buffer<gc_obj_ptr> gc_obj_ptr_buffer;
	
	typedef std::function<gc_obj_ptr (gc_obj_ptr, gc_obj_ptr_buffer buffer)> user_function;
	
	//is used as the body of a fn.
	class user_fn : cclj_noncopyable
	{
		user_function	body;

		user_fn() {}

	public:
		
		enum { cclj_type = type_ids::user_function };
		friend struct lang_type_creator;

		user_function&		get_body()					{ return body; }
		void set_body( user_function bd )				{ body = bd; }
		
		static lang_type_ptr<user_fn> create( garbage_collector_ptr gc, const char* file, int line )
		{
			return lang_type_creator::do_create_lang_type<user_fn>( gc, file, line );
		}
	};
}
#endif