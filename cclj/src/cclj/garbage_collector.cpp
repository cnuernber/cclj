//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/garbage_collector.h"
#include <cstring>
#include "cclj/algo_util.h"

using namespace cclj;
namespace cclj {

}

namespace {

	typedef vector<gc_object*> obj_ptr_list;
	typedef unordered_map<gc_object*,uint32_t> obj_ptr_int_map;

	class gc_obj_vector_set
	{
		obj_ptr_list		object_list;
		obj_ptr_int_map		object_map;
	public:

		bool insert( gc_object* obj )
		{
			pair<obj_ptr_int_map::iterator,bool> insert_item 
					= object_map.insert( make_pair( obj, (uint32_t)object_list.size() ) );
			if ( insert_item.second )
				object_list.push_back( obj );
			return insert_item.second;
		}

		bool erase( gc_object* obj )
		{
			obj_ptr_int_map::iterator iter = object_map.find( obj );
			if ( iter != object_map.end() )
			{
				uint32_t itemIdx = iter->second;
				object_map.erase( iter );
				//perform a replace-with-last operation.
				gc_object* end = object_list.back();
				if ( end != obj )
				{
					object_map[end] = itemIdx;
					object_list[itemIdx] = end;
				}
				object_list.pop_back();
				return true;
			}
			return false;
		}

		bool contains( gc_object* obj )
		{
			return object_map.find(obj) != object_map.end();
		}

		obj_ptr_list::iterator begin() { return object_list.begin(); }
		obj_ptr_list::iterator end() { return object_list.end(); }
		void clear() { object_list.clear(); object_map.clear(); }

		const_gc_object_raw_ptr_buffer objects() { return const_gc_object_raw_ptr_buffer( object_list ); }
	};
	
	//TODO - 
	//Implement a single-threaded garbage collector
	//that uses one or more nurseries and a final mark/sweep long-lived
	//area.  For multithreaded use, we will implement a larger object
	//that returns minor collectors meant for mainly single-threaded use and these
	//minor collectors will communicate when necessary (minor object becomes visible to
	//multiple threads).


	//main collector is stop-the-world, mark-sweep.  
	//Start with this and then add other ones in slowly.
	struct gc_mark_sweep_impl : public garbage_collector
	{
		allocator_ptr						alloc;
		gc_obj_vector_set					roots;
		obj_ptr_list						all_objects;
		obj_ptr_list						all_objects_temp;
		obj_ptr_int_map						locked_objects;
		gc_object_flag_values::val			last_mark;

		obj_ptr_list						mark_buffers[2];
		
			

		gc_mark_sweep_impl( allocator_ptr _alloc )
			: alloc( _alloc )
			, last_mark( gc_object_flag_values::mark_left )
		{
		}

		~gc_mark_sweep_impl()
		{
			for_each (all_objects.begin(), all_objects.end()
						,  [this](gc_object* obj) 
			{ 
				unchecked_deallocate_object( *obj ); 
			} );
			all_objects.clear();
			roots.clear();
		}

		virtual gc_object& allocate_object( size_t len, uint8_t alignment
													, object_constructor constructor
													, file_info alloc_info )
		{
			uint8_t* object_data  = alloc->allocate( len, alignment, alloc_info );
			//exception safety
			scoped_allocation __alloc_scope( alloc, object_data );
			gc_object* retval = constructor( object_data, len );
			if ( !retval ) { throw runtime_error( "constructor failure in allocate_object" ); }
			__alloc_scope.forget();
			all_objects.push_back( retval );
			return *retval;
		}

		virtual uint8_t* allocate( size_t len, uint8_t alignment, file_info alloc_info )
		{
			return alloc->allocate( len, alignment, alloc_info );
		}
		
		virtual void deallocate( void* data )
		{
			alloc->deallocate( data );
		}

		virtual alloc_info get_alloc_info( void* data )
		{
			return alloc->get_alloc_info( data );
		}

		void set_root_or_locked( gc_object& object, bool root, bool locked )
		{
			auto was_root_or_locked = object.flags.is_root() || object.flags.is_locked();
			auto is_root_or_locked = root || locked;
			if ( was_root_or_locked != is_root_or_locked ) {
				if ( is_root_or_locked ) {
					assert( !roots.contains( &object ) );
					roots.insert( &object );
				}
				else {
					assert( roots.contains( &object ) );
					roots.erase( &object );
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

		alloc_info object_alloc_info( gc_object& obj )
		{
			uint8_t* memStart = (uint8_t*)&obj;
			return alloc->get_alloc_info( memStart );
		}

		virtual void lock( gc_object& obj )
		{
			pair<obj_ptr_int_map::iterator,bool> inserter = locked_objects.insert( make_pair( &obj, 0 ) );
			++inserter.first->second;
			set_root_or_locked( obj, obj.flags.is_root(), true );
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
			gc_object* obj_buffer[64] = {0};
			uint32_t obj_index = 0;
			auto alloc_info = obj.get_gc_refdata_alloc_info();

			uint8_t* ref_data = alloc->allocate( alloc_info.alloc_size, alloc_info.alignment, CCLJ_IMMEDIATE_FILE_INFO() );
			scoped_allocation _ref_scope( alloc, ref_data );
			obj.initialize_gc_refdata( ref_data );
			for ( uint32_t num_objects = obj.get_gc_references( obj_buffer, 64, ref_data );
				num_objects; num_objects = obj.get_gc_references( obj_buffer, 64, ref_data ) )
			{
				obj_index += num_objects;
				for_each( obj_buffer, obj_buffer + num_objects
					, [&](gc_object* mark_obj )
				{
					if ( mark_obj->flags.has_value( current_mark ) == false )
						mark_buffer.push_back( mark_obj );
				} );
			}
		}

		int increment_mark_buffer_index( int oldIndex ) 
		{
			if ( oldIndex ) return 0;
			return 1;
		}

		void unchecked_deallocate_object( gc_object& obj )
		{
			obj.gc_release();
			alloc->deallocate( &obj );
		}

		void deallocate_object( gc_object& obj ) 
		{
			if ( obj.flags.is_root() ||  obj.flags.is_locked() )
				throw std::runtime_error( "bad object in deallocate_object" );
			unchecked_deallocate_object( obj );
		}

		virtual void perform_gc()
		{
			//toggle the marker.
			gc_object_flag_values::val current_mark = ( last_mark == gc_object_flag_values::mark_left ? 
															gc_object_flag_values::mark_right 
															: gc_object_flag_values::mark_left );
			//Run through all roots, marking anything reachable that hasn't been marked.
			//Ensure to remove the mark from last time.
			int mark_buffer_index = 0;

			mark_buffers[mark_buffer_index].assign( roots.begin(), roots.end() );
			while( mark_buffers[mark_buffer_index].empty() == false ) {
				obj_ptr_list& current_buffer(mark_buffers[mark_buffer_index]);
				mark_buffer_index = increment_mark_buffer_index( mark_buffer_index );
				obj_ptr_list& next_buffer( mark_buffers[mark_buffer_index] );
				next_buffer.clear();
				for_each( current_buffer.begin(), current_buffer.end()
							, [&](gc_object* obj) { mark_object( *obj, current_mark, next_buffer ); } );
			}

			last_mark = current_mark;

			//this step may not be the most efficient way to do this but it is so concise it 
			//is truly hard to resist.
			all_objects_temp.clear();
			for_each( all_objects.begin(), all_objects.end()
					, [=]( gc_object* obj ) 
					{ 
						if ( obj->flags.has_value( current_mark ) )
							all_objects_temp.push_back( obj );
						else
							deallocate_object( *obj );
					} );

			swap( all_objects, all_objects_temp );
		}
		
		virtual const_gc_object_raw_ptr_buffer roots_and_locked_objects() { return roots.objects(); }
		
		virtual const_gc_object_raw_ptr_buffer all_live_objects() { return all_objects; }
		
		virtual allocator_ptr allocator() { return alloc; }
	};
}

shared_ptr<garbage_collector> garbage_collector::create_mark_sweep( allocator_ptr alloc )
{
	return make_shared<gc_mark_sweep_impl>( alloc );
}