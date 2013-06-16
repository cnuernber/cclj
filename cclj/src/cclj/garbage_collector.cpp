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

	typedef vector<gc_object_base*> obj_ptr_list;
	typedef unordered_map<gc_object_base*,uint32_t> obj_ptr_int_map;

	class gc_obj_vector_set
	{
		obj_ptr_list		object_list;
		obj_ptr_int_map		object_map;
	public:

		bool insert( gc_object_base* obj )
		{
			pair<obj_ptr_int_map::iterator,bool> insert_item 
					= object_map.insert( make_pair( obj, (uint32_t)object_list.size() ) );
			if ( insert_item.second )
				object_list.push_back( obj );
			return insert_item.second;
		}

		bool erase( gc_object_base* obj )
		{
			obj_ptr_int_map::iterator iter = object_map.find( obj );
			if ( iter != object_map.end() )
			{
				uint32_t itemIdx = iter->second;
				object_map.erase( iter );
				//perform a replace-with-last operation.
				gc_object_base* end = object_list.back();
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

		bool contains( gc_object_base* obj )
		{
			return object_map.find(obj) != object_map.end();
		}

		obj_ptr_list::iterator begin() { return object_list.begin(); }
		obj_ptr_list::iterator end() { return object_list.end(); }
		void clear() { object_list.clear(); object_map.clear(); }

		const_gc_object_base_raw_ptr_buffer objects() { return const_gc_object_base_raw_ptr_buffer( object_list ); }
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
		string_table_ptr					str_table;
		class_system_ptr					cls_system;
		string_table_str					_ref_obj_type;
		string_table_str					_array_type;
		string_table_str					_hash_table_type;
		
			

		gc_mark_sweep_impl( allocator_ptr _alloc, string_table_ptr _str_table
				, class_system_ptr _cls_system )
			: alloc( _alloc )
			, last_mark( gc_object_flag_values::mark_left )
			, str_table( _str_table )
			, cls_system( _cls_system )
			, _ref_obj_type( str_table->register_str( "objref_t" ) )
			, _array_type( _str_table->register_str( "gc_array" ) )
			, _hash_table_type( _str_table->register_str( "gc_hash_table" ) )
		{
		}

		~gc_mark_sweep_impl()
		{
			for_each (all_objects.begin(), all_objects.end()
						,  [this](gc_object_base* obj) 
			{ 
				unchecked_deallocate_object( *obj ); 
			} );
			all_objects.clear();
			roots.clear();
		}

		template<typename tobj_type>
		pair<tobj_type*, uint8_t*> allocate_t( uint32_t num_bytes, uint32_t alignment, const char* file, int line )
		{
			uint32_t obj_size = align_number( sizeof(tobj_type), alignment );
			uint32_t instance_size = num_bytes;
			uint8_t* newmem = (uint8_t*)alloc->allocate( obj_size + instance_size, alignment, file, line );
			new (newmem) tobj_type(type);
			uint8_t* obj_mem = newmem + obj_size;
			tobj_type* retval = reinterpret_cast<tobj_type*>( newmem );

			if ( instance_size ) memset( obj_mem, 0, instance_size );

			all_objects.push_back( retval );
			return pair<tobj_type*, uint8_t*>( retval, obj_mem );
		}
		
		virtual gc_object& allocate_object( class_definition& type, const char* file, int line )
		{
			pair<gc_object*,uint8_t*> retval = allocate_t<gc_object>( type.instance_size(), type.instance_alignment(), file, line );
			return *retval.first;
		}

		virtual gc_array_object& allocate_array( class_definition& type, size_t initial_num_items
																		, const char* file, int line )
		{
			pair<gc_array_object*,uint8_t*> retval = allocate_t<gc_array_object>( type.instance_size() * initial_num_items
																				, type.instance_alignment(), file, line );
			retval.first->count = initial_num_items;
			return *retval.first;
		}

		/*
		virtual gc_hash_table_object& allocate_hashtable( class_definition& key_type, class_definition& value_type
																					, const char* file, int line )
		{
		}
		*/
		
		virtual pair<void*,size_t> reallocate( gc_reallocatable_object& in_object, size_t new_size_in_bytes, const char* file, int line )
		{
			if ( !in_object.flags.is_locked() )
				throw runtime_error( "reallocated called on unlocked object" );

			pair<void*,size_t> existing_size = get_object_data( in_object );
			if ( existing_size.second == new_size_in_bytes )
				return existing_size;

			pair<void*,size_t> contig_data = get_contiguous_object_data( in_object );
			bool was_contiguous = is_object_contiguous( in_object );
			//favour contiguous data over non-contiguous data.
			pair<void*,size_t> retval( nullptr, 0 );
			if ( new_size_in_bytes <= contig_data.second )
			{
				retval = pair<void*, size_t>( contig_data.first, new_size_in_bytes );
				in_object.data_ptr = contig_data.first;
			}
			else
			{
				//Resize the area to be exactly what is specified if possible.  This allows programs
				//to release memory when necessary.
				auto alloc_info = object_alloc_info( in_object );
				void* newmem = alloc->allocate( new_size_in_bytes, alloc_info.alignment, file, line );
				retval = pair<void*,size_t>( newmem, new_size_in_bytes );
				in_object.data_ptr = newmem;
			}

			//Here is where copy construction and deletion would happen.
			if ( retval.first != existing_size.first )
			{
				memcpy( retval.first, existing_size.first, std::min( existing_size.second, new_size_in_bytes ) );
				if ( !was_contiguous )
					alloc->deallocate( existing_size.first );
			}
			
			if ( gc_array_object::is_specific_object( in_object ) )
			{
				gc_array_object& new_obj = gc_object_traits::cast<gc_array_object>(in_object );
				new_obj.count = new_size_in_bytes / new_obj.type.instance_size();
			}

			return retval;
		}

		void set_root_or_locked( gc_object_base& object, bool root, bool locked )
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
		
		virtual void mark_root( gc_object_base& object )
		{
			set_root_or_locked( object, true, object.flags.is_locked() );
		}

		virtual void unmark_root( gc_object_base& object )
		{
			set_root_or_locked( object, false, object.flags.is_locked() );
		}

		virtual bool is_root( gc_object_base& object )
		{
			return object.flags.is_root();
		}

		virtual void update_reference( gc_object_base& /*parent*/, gc_object_base* /*child*/ )
		{
			//write barrier isn't particularly useful right now.
		}

		alloc_info object_alloc_info( gc_object_base& obj )
		{
			uint8_t* memStart = (uint8_t*)&obj;
			return alloc->get_alloc_info( memStart );
		}

		bool is_object_contiguous( gc_object_base& obj )
		{
			if ( !gc_reallocatable_object::is_specific_object( obj ) )
				return true;
			gc_reallocatable_object& realloc( gc_object_traits::cast<gc_reallocatable_object&>( obj ) );
			auto alloc_info = object_alloc_info( obj );
			uint32_t obj_size = align_number( (uint32_t)gc_object_traits::obj_size(obj), alloc_info.alignment );
			uint8_t* memStart = (uint8_t*)&obj;
			return realloc.data_ptr == reinterpret_cast<void*>( memStart + obj_size );
		}

		pair<void*,size_t> get_contiguous_object_data( gc_object_base& obj )
		{
			uint8_t* memStart = (uint8_t*)&obj;
			auto alloc_info = alloc->get_alloc_info( memStart );
			uint32_t obj_size = align_number( (uint32_t)gc_object_traits::obj_size(obj), alloc_info.alignment );
			uint32_t data_size = alloc_info.alloc_size - obj_size;
			if ( data_size )
				return make_pair( memStart + obj_size, alloc_info.alloc_size - obj_size );
			return pair<void*,size_t>( nullptr, 0 );
		}

		pair<void*,size_t> get_object_data( gc_object_base& obj )
		{
			if ( is_object_contiguous(obj) )
			{
				return get_contiguous_object_data( obj );
			}
			else
			{
				gc_reallocatable_object& realloc( gc_object_traits::cast<gc_reallocatable_object&>( obj ) );
				auto alloc_info = alloc->get_alloc_info( realloc.data_ptr );
				return make_pair( realloc.data_ptr, alloc_info.alloc_size );
			}
		}

		virtual pair<void*,size_t> lock( gc_object_base& obj )
		{
			pair<obj_ptr_int_map::iterator,bool> inserter = locked_objects.insert( make_pair( &obj, 0 ) );
			++inserter.first->second;
			set_root_or_locked( obj, obj.flags.is_root(), true );
			return get_object_data( obj );
		}

		virtual void unlock( gc_object_base& obj )
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

		void mark_object( gc_object_base& obj, gc_object_flag_values::val current_mark, obj_ptr_list& mark_buffer )
		{
			if ( obj.flags.has_value( current_mark ) )
				return;

			obj.flags.set( current_mark, true );
			obj.flags.set( last_mark, false );
			gc_object_base* obj_buffer[64];
			size_t reference_index = 0;
			if ( obj.type )
			{
				class_definition_ptr class_def = cls_system->find_definition( obj.type );
				if ( !class_def ) throw runtime_error( "failed to find type of item" );
				//find all the objref or objref_ properties
				uint8_t* obj_buffer = reinterpret_cast<uint8_t*>( get_object_data(obj).first );
				uint32_t instance_size = class_def->instance_size();

				for ( uint32_t idx = 0, end = obj.count; idx < end; ++idx )
				{
					uint8_t* local_obj = obj_buffer + idx * instance_size;
					auto obj_props = class_def->all_properties();
					for_each( obj_props.begin(), obj_props.end()
						, [&] ( const property_entry& entry )
					{
						if ( entry.definition.type == _ref_obj_type )
						{
							gc_object_base** prop_start_ptr = reinterpret_cast<gc_object_base**>( local_obj + entry.offset );
							for ( uint32_t idx = 0, end = entry.definition.count; idx < end; ++idx ) 
							{
								gc_object_base* mark_obj = prop_start_ptr[idx];
								if ( mark_obj && mark_obj->flags.has_value( current_mark ) == false )
									mark_buffer.insert( mark_buffer.end(), mark_obj );
							}
						}
					} );
				}
			}
		}

		int increment_mark_buffer_index( int oldIndex ) 
		{
			if ( oldIndex ) return 0;
			return 1;
		}

		void unchecked_deallocate_object( gc_object_base& obj )
		{
			if ( !is_object_contiguous( obj ) )
			{
				alloc->deallocate( get_object_data( obj ).first );
			}
			alloc->deallocate( &obj );
		}

		void deallocate_object( gc_object_base& obj ) 
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
							, [&](gc_object_base* obj) { mark_object( *obj, current_mark, next_buffer ); } );
			}

			last_mark = current_mark;

			//this step may not be the most efficient way to do this but it is so concise it 
			//is truly hard to resist.
			all_objects_temp.clear();
			for_each( all_objects.begin(), all_objects.end()
					, [=]( gc_object_base* obj ) 
					{ 
						if ( obj->flags.has_value( current_mark ) )
							all_objects_temp.push_back( obj );
						else
							deallocate_object( *obj );
					} );

			swap( all_objects, all_objects_temp );
		}
		
		virtual const_gc_object_base_raw_ptr_buffer roots_and_locked_objects() { return roots.objects(); }
		
		virtual const_gc_object_base_raw_ptr_buffer all_live_objects() { return all_objects; }
		
		virtual string_table_str hash_table_type() const { return _hash_table_type; }
		
		virtual allocator_ptr allocator() { return alloc; }
		virtual string_table_ptr string_table() { return str_table; }
		virtual class_system_ptr class_system() { return cls_system; }
	};
}

shared_ptr<garbage_collector> garbage_collector::create_mark_sweep( 
	allocator_ptr alloc
	, string_table_ptr str_table
	, class_system_ptr cls_system )
{
	return make_shared<gc_mark_sweep_impl>( alloc, str_table, cls_system );
}