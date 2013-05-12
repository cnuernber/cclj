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
		virtual size_t get_outgoing_references( gc_object& object, size_t index, gc_object** buffer, size_t bufferLen ) = 0;
		virtual void object_deallocated( gc_object& object ) = 0;
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

	struct gc_object_flags : public flags<gc_object_flag_values::val,uint16_t>
	{
		bool is_root() const { return has_value( gc_object_flag_values::root ); }
		void set_root( bool val ) { set( gc_object_flag_values::root, val ); }

		bool is_locked() const { return has_value( gc_object_flag_values::locked ); }
		void set_locked( bool val ) { set( gc_object_flag_values::locked, val ); }

		bool is_marked_left() const { return has_value( gc_object_flag_values::mark_left ); }
		void set_marked_left( bool val ) { set( gc_object_flag_values::mark_left, val ); }
		
		bool is_marked_right() const { return has_value( gc_object_flag_values::mark_right ); }
		void set_marked_right( bool val ) { set( gc_object_flag_values::mark_right, val ); }
	};

	class gc_object
	{
		gc_object( const gc_object& );
		gc_object& operator=( const gc_object& );
	public:

		//do not create these yourself; create them with the garbage collector
		//member data may or may not follow this object immediately, depends on if the collector
		//is a copying or noncopying collector.
		gc_object() : user_flags( 0 ), user_data( 0 ) {}
		gc_object_flags flags;
		uint16_t		user_flags;
		uint32_t		user_data;
	};

	//It should be noted that the collector could be copying.  
	class garbage_collector
	{
	protected:
		virtual ~garbage_collector(){}
	public:

		friend class shared_ptr<garbage_collector>;

		virtual gc_object& allocate( size_t size, const char* file, int line ) = 0;
		virtual void mark_root( gc_object& object ) = 0;
		virtual void unmark_root( gc_object& object ) = 0;
		virtual bool is_root( gc_object& object ) = 0;
		virtual void update_reference( gc_object& parent, gc_object* child ) = 0;
		virtual pair<void*,size_t> lock( gc_object& obj ) = 0;
		virtual void unlock( gc_object& obj ) = 0;
		virtual void perform_gc() = 0;

		static shared_ptr<garbage_collector> create_mark_sweep( allocator_ptr alloc, reference_tracker_ptr refTracker );
	};

	typedef shared_ptr<garbage_collector> garbage_collector_ptr;

}

#endif