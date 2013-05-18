//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/state.h"

using namespace cclj;

namespace 
{
	struct ref_tracker_impl : public reference_tracker
	{
		context* last_context;
		string_object_map::iterator next_iter;
		string_object_map::iterator end_iter;
		size_t next_idx;

		ref_tracker_impl() : last_context( nullptr ), next_idx( 0 ) {}

		virtual size_t get_outgoing_references( gc_object& object, pair<void*,size_t> data
												, size_t index, gc_object** buffer, size_t buffer_len )
		{
			switch( object.user_flags )
			{
			case type_ids::no_type_id:
				return 0;
			case type_ids::cons_cell:
				{
					cons_cell* cell = reinterpret_cast<cons_cell*>( data.first );
					size_t buf_idx = 0;
					if ( !index && cell->get_value() ) {
						buffer[buf_idx] = cell->get_value();
						++buf_idx;
					}
					if ( buf_idx < buffer_len ) {
						--index;
						if ( !index  && cell->get_next() ) {
							buffer[buf_idx] = cell->get_next();
							++buf_idx;
						}
					}
					return buf_idx;
				}
			case type_ids::number:
				return 0;
			case type_ids::symbol:
				return 0;
			case type_ids::function:
				{
					fn* function = reinterpret_cast<fn*>( data.first );
					size_t buf_idx = 0;
					if ( !index && function->get_context() ) {
						buffer[buf_idx] = function->get_context();
						++buf_idx;
					}
					if ( buf_idx < buffer_len ) {
						--index;
						if ( !index && function->get_body() ) {
							buffer[buf_idx] = function->get_body();
							++buf_idx;
						}
					}
					return buf_idx;
				}
			case type_ids::user_function:
				return 0;
			case type_ids::context:
				{
					context* ctx = reinterpret_cast<context*>( data.first );
					if ( ctx != last_context || index != next_idx ) {
						last_context = ctx;
						next_idx = index;
						next_iter = ctx->get_values().begin();
						end_iter = ctx->get_values().end();
						advance( next_iter, next_idx );
					}

					size_t num_values = ctx->get_values().size();
					size_t next_chunk = std::min( num_values - next_idx, buffer_len );
					size_t end_idx = next_idx + next_chunk;
					gc_object** output_ptr = buffer;
					for (; next_idx < end_idx; ++next_idx, ++next_iter, ++output_ptr ) {
						*output_ptr = next_iter->second;
					}
					return next_chunk;
				}
			}
			assert(false);
			return 0;
		}
		virtual void object_deallocated( gc_object& object, pair<void*,size_t> data )
		{
			if ( object.user_flags == type_ids::context )
			{
				context* ctx = reinterpret_cast<context*>( data.first );
				ctx->~context();
			}
			else if ( object.user_flags == type_ids::user_function )
			{
				user_fn* ctx = reinterpret_cast<user_fn*>( data.first );
				ctx->~user_fn();
			}
		}
		//It should be noted that the collector could be copying.  
		virtual void object_copied( gc_object& /*object*/, pair<void*,size_t>& /*oldMem*/, pair<void*,size_t> /*newMem*/ )
		{
			assert(false);
			//possible exception here, actually
			throw std::runtime_error( "reference_tracker::object_copied is unimplemented" );
		}
	};

	struct user_fn_connection : public connection
	{
		lang_type_ptr<user_fn> fn;
		user_fn_connection( lang_type_ptr<user_fn> _fn ) : fn( _fn ) {}
		~user_fn_connection() { fn->set_body( user_function() ); }
	};

	struct state_impl : public state
	{
		garbage_collector_ptr	_gc;
		lang_type_ptr<context>	_context;
		vector<connection_ptr>  _std_functions;

		state_impl(allocator_ptr alloc)
			: _gc( garbage_collector::create_mark_sweep( alloc, make_shared<ref_tracker_impl>() ) )
		{
			_context = context::create( _gc, __FILE__, __LINE__ );

		}

		virtual gc_obj_ptr eval( const char* /*script*/
								, lang_type_ptr<context> /*script_context*/ )
		{
			return gc_obj_ptr();
		}

		virtual lang_type_ptr<context> global_context()
		{
			return _context;
		}

		virtual void set_global_context( lang_type_ptr<context> ctx )
		{
			_context = ctx;
		}
		virtual garbage_collector_ptr gc()
		{
			return _gc;
		}

		//Returns a function whos body points to the user_fn whose body is this user_function
		virtual pair<connection_ptr,lang_type_ptr<user_fn> > register_function( const user_function& input_fn )
		{
			lang_type_ptr<user_fn> item = user_fn::create( _gc, __FILE__, __LINE__ );
			item->set_body( input_fn );
			connection_ptr retval = make_shared<user_fn_connection>( item );
			return pair<connection_ptr,lang_type_ptr<cclj::user_fn> > ( retval, item);
		}
		
	};
}