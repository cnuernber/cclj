//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/gc.h"
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <limits>
#include <algorithm>
#include <cassert>

using namespace cclj;
using std::unordered_set;
using std::vector;
using std::numeric_limits;
using std::for_each;
using std::remove_if;
using std::unordered_map;
using std::make_pair;
namespace cclj {

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
	public:
		gc_object_flags flags;
	};
}

namespace {

	typedef vector<gc_object*> obj_ptr_list;
	typedef unordered_map<gc_object*,uint32_t> obj_ptr_int_map;
	
	//Implement a single-threaded garbage collector
	//that uses one or more nurseries and a final mark/sweep long-lived
	//area.  For multithreaded use, we will implement a larger object
	//that returns minor collectors meant for mainly single-threaded use and these
	//minor collectors will communicate when necessary (minor object becomes visible to
	//multiple threads).

	//main collector is stop-the-world, mark-sweep.
	struct gc_impl : public garbage_collector
	{
		allocator_ptr						alloc;
		reference_tracker_ptr				reftrack;
		obj_ptr_list						roots;
		obj_ptr_list						all_objects;
		obj_ptr_int_map						locked_objects;
		gc_object_flag_values::val			last_mark;

		obj_ptr_list						mark_buffers[2];
		
			

		gc_impl( allocator_ptr _alloc, reference_tracker_ptr _reftrack )
			: alloc( _alloc )
			, reftrack( _reftrack )
			, last_mark( gc_object_flag_values::mark_left )
		{
		}

		~gc_impl()
		{
			for_each (all_objects.begin(), all_objects.end()
						,  [this](gc_object* obj) { reftrack->object_deallocated(*obj); alloc->deallocate( &obj ); } );
			all_objects.clear();
			roots.clear();
		}
		
		virtual gc_object& allocate( size_t size, const int8_t* file, int line )
		{
			size_t alloc_size = sizeof( gc_object ) + size;
			uint8_t* newmem = (uint8_t*)alloc->allocate( alloc_size, file, line );
			new (newmem) gc_object();
			gc_object* retval = (gc_object*)newmem;
			all_objects.push_back( retval );
			return *((gc_object*)newmem);
		}

		void set_root_or_locked( gc_object& object, bool root, bool locked )
		{
			auto was_root_or_locked = object.flags.is_root() || object.flags.is_locked();
			auto is_root_or_locked = root || locked;
			if ( was_root_or_locked != is_root_or_locked ) {
				if ( is_root_or_locked ) {
					roots.push_back( &object );
				}
				else {
					roots.erase( remove_if( roots.begin(), roots.end()
								, [&](gc_object*obj) { return obj == &object; } )
							, roots.end() );
				}
			}
			object.flags.set_root( root );
			object.flags.set_locked( locked );
		}
		
		virtual void mark_root( gc_object& object )
		{
			set_root_or_locked( object, true, object.flags.is_locked() );
		}

		virtual void unmark_root( gc_object& object )
		{
			set_root_or_locked( object, false, object.flags.is_locked() );
		}

		virtual bool is_root( gc_object& object )
		{
			return object.flags.is_root();
		}

		virtual void update_reference( gc_object& /*parent*/, gc_object* /*child*/ )
		{
			//write barrier isn't particularly useful right now.
		}

		virtual pair<void*,size_t> lock( gc_object& obj )
		{
			pair<obj_ptr_int_map::iterator,bool> inserter = locked_objects.insert( make_pair( &obj, 0 ) );
			++inserter.first->second;
			set_root_or_locked( obj, obj.flags.is_root(), true );
			uint8_t* memStart = (uint8_t*)&obj;
			uint8_t* userMem = memStart + sizeof( gc_object );
			return make_pair( userMem, alloc->get_alloc_size( memStart ) );
		}

		virtual void unlock( gc_object& obj )
		{
			if ( obj.flags.is_locked() == false )
				return;

			obj_ptr_int_map::iterator iter = locked_objects.find( &obj );
			if ( iter != locked_objects.end() )
			{
				if ( iter->second )
					--iter->second;
				if ( !iter->second )
				{
					locked_objects.erase( &obj );
					set_root_or_locked( obj, obj.flags.is_root(), false );
				}
			}
		}

		void mark_object( gc_object& obj, gc_object_flag_values::val current_mark, obj_ptr_list& mark_buffer )
		{
			if ( obj.flags.has_value( current_mark ) )
				return;

			obj.flags.set( current_mark, true );
			obj.flags.set( last_mark, false );
			gc_object* obj_buffer[64];
			size_t reference_index = 0;
			for ( size_t reference_count = reftrack->get_outgoing_references( obj, reference_index, obj_buffer, 64 );
					reference_count != 0; 
					reference_count = reftrack->get_outgoing_references( obj, reference_index, obj_buffer, 64 ) )
			{
				mark_buffer.insert( mark_buffer.end(), obj_buffer + 0, obj_buffer + reference_count );
				reference_index += reference_count;
			}
		}

		int increment_mark_buffer_index( int& oldIndex ) 
		{
			if ( oldIndex ) return 0;
			return 1;
		}

		void deallocate_object( gc_object& obj ) 
		{
			assert( obj.flags.is_root() == false && obj.flags.is_locked() == false );
			reftrack->object_deallocated( obj );
			alloc->deallocate( &obj );
		}

		virtual void perform_gc()
		{
			//toggle the marker.
			gc_object_flag_values::val current_mark = last_mark == gc_object_flag_values::mark_left ? gc_object_flag_values::mark_right : gc_object_flag_values::mark_left;
			//Run through all roots, marking anything reachable that hasn't been marked.
			//Ensure to remove the mark from last time.
			int mark_buffer_index = 0;

			mark_buffers[mark_buffer_index] = roots;
			while( mark_buffers[mark_buffer_index].empty() == false ) {
				obj_ptr_list& current_buffer(mark_buffers[mark_buffer_index]);
				increment_mark_buffer_index( mark_buffer_index );
				obj_ptr_list& next_buffer( mark_buffers[mark_buffer_index] );
				next_buffer.clear();
				for_each( current_buffer.begin(), current_buffer.end()
							, [&](gc_object* obj) { mark_object( *obj, current_mark, next_buffer ); } );
			}

			last_mark = current_mark;

			//this step may not be the most efficient way to do this but it is so concise it 
			//is truly hard to resist.
			obj_ptr_list::iterator remove = remove_if( all_objects.begin(), all_objects.end()
				, [=]( gc_object* obj ) { return obj->flags.has_value( current_mark ) == false; } );

			for_each( remove, all_objects.end(), [this](gc_object* obj) { deallocate_object( *obj ); } );
			all_objects.erase( remove, all_objects.end() );
		}
	};
}