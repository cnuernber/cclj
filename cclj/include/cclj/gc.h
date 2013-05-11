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

#include <memory>
#include <utility>
#include <cstdint>

namespace cclj
{
	using std::shared_ptr;
	using std::pair;
	class allocator
	{
	protected:
		virtual ~allocator(){}
	public:
		friend class shared_ptr<allocator>;
		virtual void* allocate( size_t size, const int8_t* file, int line ) = 0;
		virtual size_t get_alloc_size( void* ptr ) = 0;
		virtual void deallocate( void* memory ) = 0;
	};

	typedef shared_ptr<allocator> allocator_ptr;

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

	//It should be noted that the collector could be copying.  
	class garbage_collector
	{
	protected:
		virtual ~garbage_collector(){}
	public:

		friend class shared_ptr<garbage_collector>;

		virtual gc_object& allocate( size_t size, const int8_t* file, int line ) = 0;
		virtual void mark_root( gc_object& object ) = 0;
		virtual void unmark_root( gc_object& object ) = 0;
		virtual bool is_root( gc_object& object ) = 0;
		virtual void update_reference( gc_object& parent, gc_object* child ) = 0;
		virtual pair<void*,size_t> lock( gc_object& obj ) = 0;
		virtual void unlock( gc_object& obj ) = 0;
		virtual void perform_gc() = 0;

		static shared_ptr<garbage_collector> create( allocator_ptr alloc, reference_tracker_ptr refTracker );
	};

	typedef shared_ptr<garbage_collector> garbage_collector_ptr;

}

#endif