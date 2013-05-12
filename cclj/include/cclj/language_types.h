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

namespace cclj
{
	struct type_ids
	{
		enum val
		{
			no_type_id =		 0,
			cons_cell =		1 << 0,
		};
	};
	

	template<typename lang_type>
	lang_type* type_cast<lang_type>( gc_object& val, garbage_collector& gc ) 
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
			type = type_cast<lang_type>( *val, gc );
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

	class cons_cell
	{
		gc_object* value;
		gc_object* next;
		cons_cell() : value( nullptr ), next( nullptr ) {}

		cons_cell( const cons_cell& );
		cons_cell& operator=( const cons_cell& );

	public:
		enum { cclj_type = type_ids::cons_cell };

		static cclj_gc_ptr<const_cell> create( garbage_collect& gc, const char* file, int line )
		{
			gc_object& obj = gc.allocate( sizeof( cons_cell ), file, line );
			obj.user_flags = cclj_type;
			cclj_gc_ptr<const_cell> retval( gc, obj );
			new (retval.type) const_cell();
			return retval;
		}
	};
}
#endif