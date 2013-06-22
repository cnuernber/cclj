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
#include "cclj/noncopyable.h"
#include "cclj/data_buffer.h"

namespace cclj
{
	using std::shared_ptr;
	using std::pair;

	class gc_object;
	
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
		template<typename data_type, uint8_t offset, uint32_t mask>
		data_type bitset() const
		{
			uint32_t item = (((uint32_t)data) >> offset) & mask;
			return static_cast<data_type>( item );
		}
		
		template<typename data_type, uint8_t offset, uint32_t mask>
		void set_bitset( data_type item )
		{
			uint32_t new_item = item << offset;
			uint32_t offset_mask = mask << offset;
			offset_mask = ~offset_mask;
			data = data & ((storage)offset_mask);
			data = data | ((storage)new_item);
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
	protected:
		virtual ~gc_object(){}
	public:

		//Flags are used by the gc.  Do not mess with.
		gc_object_flags		flags; 
		uint16_t			user_flags; //16 bits for you!

		virtual alloc_info get_gc_refdata_alloc_info() = 0;
		virtual void initialize_gc_refdata( uint8_t* data ) = 0;
		//Return the objects referenced my this gc object.  May be called several times in succession.
		//Index will always be linearly incrementing or zero.
		virtual uint32_t get_gc_references( gc_object** buffer, uint32_t bufsize, uint32_t index, uint8_t* refdata ) = 0;
		virtual void gc_release() { this->~gc_object(); }
	};


	typedef const_data_buffer<gc_object*>  const_gc_object_raw_ptr_buffer;


	typedef function<gc_object* (uint8_t* mem, size_t memLen)> object_constructor;

	class garbage_collector
	{
	protected:
		virtual ~garbage_collector(){}
	public:

		friend class shared_ptr<garbage_collector>;
		
		//If new_size_in_bytes isn't a multiple of type-instance_size() error is thrown.
		virtual gc_object& allocate_object( size_t len, uint8_t alignment
													, object_constructor constructor
													, file_info alloc_info ) = 0;

		virtual uint8_t* allocate( size_t len, uint8_t alignment, file_info alloc_info ) = 0;
		virtual void deallocate( void* data ) = 0;
		virtual alloc_info get_alloc_info( void* alloc ) = 0;

		virtual void mark_root( gc_object& object ) = 0;
		virtual void unmark_root( gc_object& object ) = 0;
		virtual bool is_root( gc_object& object ) = 0;
		virtual void lock( gc_object& obj ) = 0;
		virtual void unlock( gc_object& obj ) = 0;
		virtual void perform_gc() = 0;
		//very transient data, useful for unit testing.
		virtual const_gc_object_raw_ptr_buffer roots_and_locked_objects() = 0;
		virtual const_gc_object_raw_ptr_buffer all_live_objects() = 0;

		virtual allocator_ptr allocator() = 0;

		static shared_ptr<garbage_collector> create_mark_sweep( allocator_ptr alloc );
	};

	typedef shared_ptr<garbage_collector> garbage_collector_ptr;


	template<typename tobj_type>
	class gc_scoped_lock
	{
		garbage_collector_ptr	_gc;
		tobj_type*				_object;

		void acquire()
		{
			if ( _object )
			{
				_gc->lock( *_object );
			}
		}
		void release()
		{
			if ( _object )
			{
				_gc->unlock( *_object );
				_object = nullptr;
			}
		}

	public:
		typedef gc_scoped_lock<tobj_type> this_type;
		typedef tobj_type	object_type;
		typedef tobj_type*	ptr_type;
		typedef tobj_type&	reference_type;

		gc_scoped_lock(garbage_collector_ptr gc = garbage_collector_ptr()) : _gc( gc ), _object( nullptr ) {}
		gc_scoped_lock(garbage_collector_ptr gc, reference_type obj ) : _gc( gc ), _object( &obj )
		{
			acquire();
		}
		gc_scoped_lock(garbage_collector_ptr gc, ptr_type obj ) : _gc( gc ), _object( obj )
		{
			acquire();
		}
		gc_scoped_lock( const this_type& other )
			: _gc( other._gc )
			, _object( other._object )
		{
			acquire();
		}
		gc_scoped_lock& operator=( const this_type& other )
		{
			if ( this != &other )
			{
				release();
				_gc = other._gc;
				_object = other._object;
				acquire();
			}
			return *this;
		}
		~gc_scoped_lock() { release(); }
		ptr_type get() const { if ( !_object ) throw runtime_error("invalid ptr dereference"); return _object; }
		garbage_collector_ptr gc() const { return _gc; }

		ptr_type operator->() const { return get(); }
		reference_type operator*() const { if ( !_object ) throw runtime_error("invalid ptr dereference"); return *_object; }

		operator bool () const { return _object != nullptr; }
		bool operator == ( const this_type& other ) const { return _object == other._object; }
		bool operator != ( const this_type& other ) const { return _object != other._object; }
	};
}

#endif