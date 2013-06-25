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
	struct object_index_lock
	{
		uint32_t	index;
		int32_t		lock_count;
		object_index_lock(uint32_t idx)
			: index( (uint32_t)idx )
			, lock_count( 0 )
		{
		}
		object_index_lock() : index( 0 ), lock_count( 0 ) {}
		void inc_lock() { ++lock_count; }
		void dec_lock() { --lock_count; }
		bool is_locked() const { return lock_count > 0; }
	};


	typedef vector<gc_object*> obj_ptr_list;
	typedef unordered_map<gc_object*,object_index_lock> obj_ptr_index_lock_map;

	class gc_obj_vector_set
	{
		obj_ptr_list			object_list;
		obj_ptr_index_lock_map	object_map;
	public:

		//returns true if the item was inserted
		bool inc_lock( gc_object& obj )
		{
			pair<obj_ptr_index_lock_map::iterator,bool> insert_item 
					= object_map.insert( make_pair( &obj, (uint32_t)object_list.size() ) );
			if ( insert_item.second )
				object_list.push_back( &obj );
			insert_item.first->second.inc_lock();
			return insert_item.second;
		}

		//returns true if the item was removed.
		bool dec_lock( gc_object& obj )
		{
			obj_ptr_index_lock_map::iterator iter = object_map.find( &obj );
			if ( iter == object_map.end() ) throw runtime_error( "implementation error" );

			iter->second.dec_lock();
			bool erase_obj = iter->second.is_locked() == false;
			if ( erase_obj )
			{
				uint32_t itemIdx = iter->second.index;
				object_map.erase( iter );
				//perform a replace-with-last operation.
				gc_object* end = object_list.back();
				if ( end != &obj )
				{
					object_map[end].index = itemIdx;
					object_list[itemIdx] = end;
				}
				object_list.pop_back();
			}
			return erase_obj;
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
		gc_obj_vector_set					_locked_objects;
		obj_ptr_list						_all_objects;
		obj_ptr_list						all_objects_temp;
		gc_object_flag_values::val			last_mark;

		obj_ptr_list						mark_buffers[2];
		
			

		gc_mark_sweep_impl( allocator_ptr _alloc )
			: alloc( _alloc )
			, last_mark( gc_object_flag_values::mark_left )
		{
		}

		~gc_mark_sweep_impl()
		{
			for_each (_all_objects.begin(), _all_objects.end()
						,  [this](gc_object* obj) 
			{ 
				unchecked_deallocate_object( *obj ); 
			} );
			_all_objects.clear();
			_locked_objects.clear();
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
			_all_objects.push_back( retval );
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

		alloc_info object_alloc_info( gc_object& obj )
		{
			uint8_t* memStart = (uint8_t*)&obj;
			return alloc->get_alloc_info( memStart );
		}

		virtual void lock( gc_object& obj )
		{
			if ( _locked_objects.inc_lock( obj ) )
				obj.gc_only_writeable_flags().set_locked(true);

			if ( !obj.flags().is_locked() ) throw runtime_error( "implementation error" );
		}

		virtual void unlock( gc_object& obj )
		{
			if ( obj.flags().is_locked() == false )
				return;

			if ( _locked_objects.dec_lock( obj ) )
				obj.gc_only_writeable_flags().set_locked( false );
		}

		void mark_object( gc_object& obj, gc_object_flag_values::val current_mark, obj_ptr_list& mark_buffer )
		{
			if ( obj.flags().has_value( current_mark ) )
				return;

			obj.gc_only_writeable_flags().set( current_mark, true );
			obj.gc_only_writeable_flags().set( last_mark, false );
			gc_object* obj_buffer[64] = {0};
			uint32_t obj_index = 0;
			auto alloc_info = obj.get_gc_refdata_alloc_info();

			uint8_t* ref_data = alloc->allocate( alloc_info.alloc_size, alloc_info.alignment, CCLJ_IMMEDIATE_FILE_INFO() );
			scoped_allocation _ref_scope( alloc, ref_data );
			obj.initialize_gc_refdata( ref_data );
			for ( uint32_t num_objects = obj.get_gc_references( obj_buffer, 64, obj_index, ref_data );
				num_objects; num_objects = obj.get_gc_references( obj_buffer, 64, obj_index, ref_data ) )
			{
				obj_index += num_objects;
				for_each( obj_buffer, obj_buffer + num_objects
					, [&](gc_object* mark_obj )
				{
					if ( mark_obj->flags().has_value( current_mark ) == false )
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
			if ( obj.flags().is_locked() )
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

			mark_buffers[mark_buffer_index].assign( _locked_objects.begin(), _locked_objects.end() );
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
			for_each( _all_objects.begin(), _all_objects.end()
					, [=]( gc_object* obj ) 
					{ 
						if ( obj->flags().has_value( current_mark ) )
							all_objects_temp.push_back( obj );
						else
							deallocate_object( *obj );
					} );

			swap( _all_objects, all_objects_temp );
		}
		
		virtual const_gc_object_raw_ptr_buffer locked_objects() { return _locked_objects.objects(); }
		
		virtual const_gc_object_raw_ptr_buffer all_objects() { return _all_objects; }
		
		virtual allocator_ptr allocator() { return alloc; }
	};
}

shared_ptr<garbage_collector> garbage_collector::create_mark_sweep( allocator_ptr alloc )
{
	return make_shared<gc_mark_sweep_impl>( alloc );
}