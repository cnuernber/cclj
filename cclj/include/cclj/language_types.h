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
#include "cclj/garbage_collector.h"
#include "cclj/string_table.h"
#include "cclj/data_buffer.h"
#include "cclj/virtual_machine.h"
#include "cclj/variant.h"

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
}
#endif