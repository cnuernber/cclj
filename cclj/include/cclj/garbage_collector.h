//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_GC_H
#define CCLJ_GC_H
#pragma once

#include "cclj/cclj.h"
#include "cclj/allocator.h"
#include "cclj/string_table.h"
#include "cclj/class_system.h"
#include "cclj/noncopyable.h"
#include "cclj/class_system_predefined_types.h"

namespace cclj
{
	using std::shared_ptr;
	using std::pair;

	class gc_object;

	class reference_tracker
	{
	protected:
		virtual ~reference_tracker(){}
	public:
		friend class shared_ptr<reference_tracker>;
		virtual size_t get_outgoing_references( gc_object& object, pair<void*,size_t> data
												, size_t index, gc_object** buffer, size_t bufferLen ) = 0;
		virtual void object_deallocated( gc_object& object, pair<void*,size_t> data ) = 0;
		//It should be noted that the collector could be copying.  
		virtual void object_copied( gc_object& object, pair<void*,size_t>& oldMem, pair<void*,size_t> newMem ) = 0;
	};

	typedef shared_ptr<reference_tracker> reference_tracker_ptr;
	
	template<typename enumType, typename storage>
	struct flags
	{
		storage data;
		flags() : data( 0 ) {}
		bool has_value( enumType val ) const
		{
			return (data & ((storage)val)) == (storage)val;
		}

		void set( enumType flag, bool value )
		{
			storage newVal = (storage)flag;
			if ( value ) {
				data = data | newVal;
			}
			else {
				storage opposite = ~newVal;
				data = data & opposite;
			}
		}
	};

	struct gc_object_flag_values
	{
		enum val
		{
			no_value =			0,
			root =				1 << 0,
			locked =			1 << 1,
			mark_left =			1 << 2,
			mark_right =		1 << 3,
		};
	};

	class gc_object_flags : public flags<gc_object_flag_values::val,uint16_t>
	{
	public:
		bool is_root() const { return has_value( gc_object_flag_values::root ); }
		void set_root( bool val ) { set( gc_object_flag_values::root, val ); }

		bool is_locked() const { return has_value( gc_object_flag_values::locked ); }
		void set_locked( bool val ) { set( gc_object_flag_values::locked, val ); }

		bool is_marked_left() const { return has_value( gc_object_flag_values::mark_left ); }
		void set_marked_left( bool val ) { set( gc_object_flag_values::mark_left, val ); }
		
		bool is_marked_right() const { return has_value( gc_object_flag_values::mark_right ); }
		void set_marked_right( bool val ) { set( gc_object_flag_values::mark_right, val ); }
	};



	class gc_object : noncopyable
	{
	public:

		//do not create these yourself; create them with the garbage collector
		//member data may or may not follow this object immediately, depends on if the collector
		//is a copying or noncopying collector.
		gc_object() : data_ptr( nullptr ), user_flags( 0 ), count( 0 ) {}
		//Type is used by the gc to allow it to track references automatically without
		//calling back to via the ref tracker.  If there is no type, then the GC assumes
		//this is a user-type and will call back to c++.
		string_table_str	type;
		void*				data_ptr;
		gc_object_flags		flags; 
		uint16_t			user_flags; //16 bits for you!
		uint32_t			count; //How many instances are valid from this object.
	};

	//uses std::unordered_map which has size operator.
	struct gc_hash_table
	{
		string_table_str _key_type;
		string_table_str _value_type;
		void*			 _table;
	};

	typedef const_data_buffer<gc_object*>  const_gc_object_raw_ptr_buffer;

	//It should be noted that the collector could be copying.  
	class garbage_collector
	{
	protected:
		virtual ~garbage_collector(){}
	public:

		friend class shared_ptr<garbage_collector>;
		
		virtual gc_object& allocate( size_t size_in_bytes, uint8_t alignment, const char* file, int line ) = 0;
		//If new_size_in_bytes isn't a multiple of type-instance_size() error is thrown.
		virtual gc_object& allocate( string_table_str type, size_t new_size_in_bytes, const char* file, int line ) = 0;
		virtual pair<void*,size_t> reallocate( gc_object& in_object, size_t new_size_in_bytes, const char* file, int line ) = 0;

		virtual void mark_root( gc_object& object ) = 0;
		virtual void unmark_root( gc_object& object ) = 0;
		virtual bool is_root( gc_object& object ) = 0;
		virtual void update_reference( gc_object& parent, gc_object* child ) = 0;
		virtual pair<void*,size_t> lock( gc_object& obj ) = 0;
		virtual void unlock( gc_object& obj ) = 0;
		virtual void perform_gc() = 0;
		//very transient data, useful for unit testing.
		virtual const_gc_object_raw_ptr_buffer roots_and_locked_objects() = 0;
		virtual const_gc_object_raw_ptr_buffer all_live_objects() = 0;

		virtual string_table_str hash_table_type() const = 0;

		virtual allocator_ptr allocator() = 0;
		virtual reference_tracker_ptr reference_tracker() = 0;
		virtual string_table_ptr string_table() = 0;
		virtual class_system_ptr class_system() = 0;

		static shared_ptr<garbage_collector> create_mark_sweep( allocator_ptr alloc
																, reference_tracker_ptr refTracker
																, string_table_ptr strtable
																, class_system_ptr cls_system );
	};

	typedef shared_ptr<garbage_collector> garbage_collector_ptr;

	
	//safe-ish public access to the gc object.
	class gc_obj_ptr
	{
		garbage_collector_ptr	_gc;
		gc_object*				_object;
		pair<void*,size_t>		_data;
		void acquire()
		{
			_data = pair<void*,size_t>( nullptr, 0 );
			if ( _object && _gc )
				_data = _gc->lock( *_object );
		}
		void release()
		{
			if ( _object && _gc )
			{
				_gc->unlock( *_object );
				_data.first = nullptr;
				_data.second = 0;
			}
			_object = nullptr;
		}
	public:

		gc_obj_ptr() : _object( nullptr ), _data( nullptr, 0 ) {}

		gc_obj_ptr(garbage_collector_ptr gc, gc_object& obj )
			: _gc( gc )
			, _object( &obj )
		{
			acquire();
		}

		gc_obj_ptr( const gc_obj_ptr& obj )
			: _gc( obj._gc )
			, _object( obj._object )
		{
			acquire();
		}

		~gc_obj_ptr()
		{
			release();
		}

		gc_obj_ptr& operator=( const gc_obj_ptr& other )
		{
			if ( _object != other._object )
			{
				release();
				_gc = other._gc;
				_object = other._object;
				acquire();
			}
			return *this;
		}
		
		gc_obj_ptr& operator=( gc_object* other )
		{
			if ( _object != other )
			{
				release();
				_object = other;
				acquire();
			}
			return *this;
		}

		operator bool () const { return _object != nullptr; }

		garbage_collector_ptr gc() const { return _gc; }
		gc_object* operator->() const { assert(_object); return _object; }
		gc_object& operator*() const { assert(_object); return *_object; }

		pair<void*,size_t> data() const { return _data; }

		gc_object* object() const { return _object; }

		//resize the data section associated with this gc object.
		pair<void*,size_t> reallocate(size_t new_size)
		{
			if ( _object == nullptr )
				return pair<void*,size_t>(nullptr,0);
			
			_data.first = nullptr;
			_data.second = 0;
			_data = _gc->reallocate( *_object, new_size, __FILE__, __LINE__ );
			return _data;
		}
	};

}

#endif