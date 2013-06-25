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
			locked =			1 << 0,
			mark_left =			1 << 1,
			mark_right =		1 << 2,
		};
	};

	//flags are maintained by the gc.   They are user-readable but do not write
	//to them.
	class gc_object_flags : public flags<gc_object_flag_values::val,uint16_t>
	{
	public:

		bool is_locked() const { return has_value( gc_object_flag_values::locked ); }
		void set_locked( bool val ) { set( gc_object_flag_values::locked, val ); }

		bool is_marked_left() const { return has_value( gc_object_flag_values::mark_left ); }
		void set_marked_left( bool val ) { set( gc_object_flag_values::mark_left, val ); }
		
		bool is_marked_right() const { return has_value( gc_object_flag_values::mark_right ); }
		void set_marked_right( bool val ) { set( gc_object_flag_values::mark_right, val ); }
	};

	class gc_object;
	typedef vector<gc_object*> obj_ptr_list;
	class mark_buffer;


	class gc_object : noncopyable
	{
	protected:
		virtual ~gc_object(){}
		//Flags are used by the gc.  Do not mess with.
		gc_object_flags		_flags; 
		uint16_t			_user_flags; //16 bits for you!
		atomic<int32_t>		_refcount;
	public:

		const gc_object_flags& flags() const { return _flags; }

		//Unless you are the gc, do not call this.
		gc_object_flags& gc_only_writeable_flags() { return _flags; }

		uint16_t user_flags() const { return _user_flags; }
		void set_user_flags( uint16_t _flags ) { _user_flags = _flags; }

		void inc_ref() { atomic_fetch_add( &_refcount, 1 ); }
		void dec_ref() { atomic_fetch_sub( &_refcount, 1 ); }
		int32_t refcount() { return _refcount; }

		//Return the objects referenced my this gc object.
		virtual void mark_references( mark_buffer& buffer ) = 0;
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

		virtual void lock( gc_object& obj ) = 0;
		virtual void unlock( gc_object& obj ) = 0;

		virtual void perform_gc() = 0;
		//very transient data, useful for unit testing.
		virtual const_gc_object_raw_ptr_buffer locked_objects() = 0;
		virtual const_gc_object_raw_ptr_buffer all_objects() = 0;

		virtual allocator_ptr allocator() = 0;

		static shared_ptr<garbage_collector> create_mark_sweep( allocator_ptr alloc );
	};

	typedef shared_ptr<garbage_collector> garbage_collector_ptr;

	//Locking is meant to be external to the object graph.  Refcounting is meant to be internal.
	//So objects external to the object graph that will not be considered should use locking.
	//Objects internal should use refcounting.
	template<typename tobj_type>
	class gc_lock_ptr
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
		typedef gc_lock_ptr<tobj_type> this_type;
		typedef tobj_type	object_type;
		typedef tobj_type*	ptr_type;
		typedef tobj_type&	reference_type;

		gc_lock_ptr(garbage_collector_ptr gc = garbage_collector_ptr()) : _gc( gc ), _object( nullptr ) {}
		gc_lock_ptr(garbage_collector_ptr gc, reference_type obj ) : _gc( gc ), _object( &obj )
		{
			acquire();
		}
		gc_lock_ptr(garbage_collector_ptr gc, ptr_type obj ) : _gc( gc ), _object( obj )
		{
			acquire();
		}
		gc_lock_ptr( const this_type& other )
			: _gc( other._gc )
			, _object( other._object )
		{
			acquire();
		}
		gc_lock_ptr& operator=( const this_type& other )
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
		~gc_lock_ptr() { release(); }
		ptr_type get() const { if ( !_object ) throw runtime_error("invalid ptr dereference"); return _object; }
		garbage_collector_ptr gc() const { return _gc; }

		ptr_type operator->() const { return get(); }
		reference_type operator*() const { if ( !_object ) throw runtime_error("invalid ptr dereference"); return *_object; }

		operator bool () const { return _object != nullptr; }
		bool operator == ( const this_type& other ) const { return _object == other._object; }
		bool operator != ( const this_type& other ) const { return _object != other._object; }
	};

	//Reference counting is used inside the object graph.
	template<typename tobj_type>
	class gc_refcount_ptr
	{
		tobj_type*				_object;
		
		void acquire()
		{
			if ( _object )
				_object->inc_ref();
		}

		void release()
		{
			if ( _object )
			{
				_object->dec_ref();
				_object = nullptr;
			}
		}

	public:
		typedef gc_refcount_ptr<tobj_type> this_type;
		typedef tobj_type	object_type;
		typedef tobj_type*	ptr_type;
		typedef tobj_type&	reference_type;

		gc_refcount_ptr() : _object( nullptr ) {}

		gc_refcount_ptr(reference_type obj ) : _object( &obj )
		{
			acquire();
		}

		gc_refcount_ptr(ptr_type obj ) : _object( obj ) 
		{
			acquire();
		}

		gc_refcount_ptr& operator=( const this_type& other )
		{
			if ( this != &other )
			{
				release();
				_object = other._object;
				acquire();
			}
			return *this;
		}

		~gc_refcount_ptr() { release(); }

		ptr_type get() const { if ( !_object ) throw runtime_error("invalid ptr dereference"); return _object; }

		ptr_type operator->() const { return get(); }
		reference_type operator*() const { if ( !_object ) throw runtime_error("invalid ptr dereference"); return *_object; }

		operator bool () const { return _object != nullptr; }
		bool operator == ( const this_type& other ) const { return _object == other._object; }
		bool operator != ( const this_type& other ) const { return _object != other._object; }
	};
	

	
	class mark_buffer : noncopyable
	{
		obj_ptr_list&				_objs;
		gc_object_flag_values::val _current_mark;

	public:
		mark_buffer( obj_ptr_list& o, gc_object_flag_values::val mark )
			: _objs( o )
			, _current_mark( mark )
		{
		}

		void mark( gc_object& obj )
		{
			if ( obj.flags().has_value( _current_mark ) == false )
				_objs.push_back( &obj );
		}

		template<typename obj_type>
		void mark( gc_refcount_ptr<obj_type>& ptr )
		{
			if ( ptr ) mark( *ptr );
		}

		template<typename obj_type>
		void mark( obj_type* ptr )
		{
			if ( ptr ) mark( *ptr );
		}
	};
}

#endif