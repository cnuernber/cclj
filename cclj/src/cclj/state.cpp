//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/state.h"
#include "cclj/virtual_machine.h"
#include <cstdlib>

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
					size_t addition = 0;
					if ( index == 0 && ctx->get_parent_context() )
					{
						*buffer = ctx->get_parent_context();
						++buffer;
						--buffer_len;
						++addition;
					}

					if ( ctx->get_parent_context() )
						--index;
					
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
					return next_chunk + addition;
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

	struct script_executor
	{
		string	_script;
		string	_file;
		int		_line;
		lang_type_ptr<context> _context;
		string::iterator script_pos;
		string::iterator end_pos;
		string_table_ptr string_table;
		register_file_ptr _registers;
		//to avoid a shitload of reverse calls
		script_executor( const char* s, const char* f
							, lang_type_ptr<context> ctx
							, string_table_ptr strt
							, register_file_ptr reg_file )
			: _script( non_null( s ) )
			, _file( non_null( f ) )
			, _line( 0 )
			, _context( ctx )
			, script_pos( _script.begin() )
			, end_pos( _script.end() )
			, string_table( strt )
			, _registers( reg_file )
		{
		}

		void eatwhite()
		{
			size_t non_white_pos = _script.find_first_not_of( "\r\n\t ", script_pos - _script.begin() );
			if ( non_white_pos != string::npos )
				script_pos = _script.begin() + non_white_pos;
			else
				script_pos = end_pos;
		}

		bool is_numeric( char val )
		{
			return val == '-' || (val >= '0' && val <= '9');
		}

		//returns the register file index and the type.
		pair<uint32_t,type_ids::val> parse_value()
		{
			char val = *script_pos;
			size_t val_end = _script.find_first_of( "\r\n\t ", script_pos - _script.begin() );
			if ( val_end == string::npos )
				val_end = _script.size();
			string::iterator val_end_iter = _script.begin() + val_end;

			pair<uint32_t,type_ids::val> retval;
			if ( is_numeric( val ) )
			{
				double val = strtod( _script.c_str() + (script_pos - _script.begin() ), NULL );
				data_register datareg;
				float* dataPtr = reinterpret_cast<float*>( &datareg._data );


				retval.second = 
				gc_object& newObj = _context.gc()->allocate( 0, _file.c_str(), _line );
				newObj.user_flags = type_ids::number;
				number_to_gc_object( newObj , static_cast<cclj_number>( val ) );
				retval = gc_obj_ptr( _context.gc(), newObj );
			}
			else
			{
				lang_type_ptr<symbol> new_symbol = symbol::create( _context.gc(), _file.c_str(), _line );
				size_t start = script_pos - _script.begin();
				size_t end = val_end_iter - _script.begin();
				string temp_val = _script.substr( start, end - start );
				new_symbol->set_name( string_table->register_str( temp_val.c_str() ) );
				retval = new_symbol.obj();
			}
			script_pos = val_end_iter;
			return retval;
		}

		gc_obj_ptr parse_list()
		{
			typedef vector<lang_type_ptr<cons_cell> > list_vec;

			garbage_collector_ptr gc = _context.gc();
			eatwhite();
			list_vec retval;
			if ( script_pos == end_pos )
				return gc_obj_ptr();
			if ( *script_pos == '(' )
			{
				++script_pos;
				eatwhite();
				while ( script_pos != end_pos && *script_pos != ')' )
				{
					lang_type_ptr<cons_cell> cell = cons_cell::create( gc, _file.c_str(), _line );
					retval.push_back( cell );
					char next_char = *script_pos;
					if ( next_char == '(' )
					{
						cell->set_value( parse_list().object() );
					}
					else
					{
						cell->set_value( parse_value().object() );
					}
					eatwhite();
				}
			}
			if ( retval.size() )
			{
				for( size_t idx = 0, end = retval.size() - 1; idx < end; ++idx )
					retval[idx]->set_next( retval[idx+1].object() );
				return retval.front().obj();
			}
			return gc_obj_ptr();
		}
		
		gc_obj_ptr parse()
		{
			return parse_list();
		}

		gc_obj_ptr resolve( gc_object* obj )
		{
			if ( !obj ) return gc_obj_ptr();
			lang_type_ptr<symbol> sym( gc_obj_ptr( _context.gc(), *obj ) );
			if ( !sym )
			{
				if ( obj->user_flags == type_ids::cons_cell )
					return execute_object( gc_obj_ptr( _context.gc(), *obj ) );
				else
					return gc_obj_ptr(_context.gc(), *obj);
			}
			string_table_str name = sym->get_name();
			string_object_map::iterator iter = _context->get_values().find( name );
			if ( iter != _context->get_values().end() )
			{
				return gc_obj_ptr( _context.gc(), *iter->second );
			}
			return gc_obj_ptr();
		}

		lang_type_ptr<fn> resolve_function( gc_object* obj )
		{
			return lang_type_ptr<fn>( resolve( obj ) );
		}

		lang_type_ptr<cons_cell> next_cell( lang_type_ptr<cons_cell> cell )
		{
			if ( cell )
				return lang_type_ptr<cons_cell>( gc_obj_ptr( _context.gc(), *cell->get_next() ) );
			return lang_type_ptr<cons_cell>();
		}

		gc_obj_ptr execute_object( gc_obj_ptr val )
		{
			lang_type_ptr<cons_cell> first_cell( val );
			if ( !first_cell )
				return gc_obj_ptr();
			lang_type_ptr<fn> symbol_res = resolve_function( first_cell->get_value() );
			if ( symbol_res && symbol_res->get_body() )
			{
				vector<gc_obj_ptr> fn_args;
				for ( lang_type_ptr<cons_cell> arg_cell = next_cell( first_cell );
						arg_cell; arg_cell = next_cell( arg_cell ) )
				{
					fn_args.push_back( gc_obj_ptr( _context.gc(), *arg_cell->get_value() ) );
				}
				//Resolve from the back to the front.
				for_each( fn_args.rbegin(), fn_args.rend()
							, [this](gc_obj_ptr& arg) {
								arg = resolve(arg.object());
							  } );

				gc_obj_ptr call_context = gc_obj_ptr( _context.obj() );
				if ( symbol_res->get_context() )
					call_context = symbol_res->get_context();

				gc_obj_ptr_buffer args( fn_args );
				gc_object* body = symbol_res->get_body();
				lang_type_ptr<user_fn> fn_ptr( gc_obj_ptr( _context.gc(), *body ) );
				if ( fn_ptr )
				{
					user_function& body_fn(fn_ptr->get_body());
					if ( body_fn )
						return body_fn( call_context, args );
					else
						assert(false);
				}
				else
				{
					assert(false);
				}
			}
			return gc_obj_ptr();
		}
		
		gc_obj_ptr execute()
		{
			gc_obj_ptr retval;

			for ( gc_obj_ptr next_parse_obj = parse(); next_parse_obj; next_parse_obj = parse() )
				retval = execute_object( next_parse_obj );

			return retval;
		}
	};

	struct state_impl : public state
	{
		garbage_collector_ptr	_gc;
		lang_type_ptr<context>	_context;
		vector<connection_ptr>  _std_functions;
		string_table_ptr		_str_table;

		state_impl(allocator_ptr alloc)
			: _gc( garbage_collector::create_mark_sweep( alloc, make_shared<ref_tracker_impl>() ) )
			, _str_table( string_table::create() )
		{
			_context = context::create( _gc, __FILE__, __LINE__ );
			register_std_function( "+"
				, [this]( gc_obj_ptr context, gc_obj_ptr_buffer objects ) 
					{ 
						return add( context, objects ); 
					} );
		}

		virtual gc_obj_ptr eval( const char* script
								, const char* file
								, lang_type_ptr<context> script_context )
		{
			lang_type_ptr<context> exe_context = script_context ? script_context : _context;
			script_executor executor( script, file, exe_context, _str_table );
			return executor.execute();
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

		void register_std_function( const char* name, const user_function& input_fn )
		{
			pair<connection_ptr, lang_type_ptr<user_fn> > register_result = register_function( input_fn );
			_std_functions.push_back( register_result.first );
			lang_type_ptr<fn> function = fn::create( _gc, __FILE__, __LINE__ );
			function->set_context( _context.object() );
			function->set_body( register_result.second.object() );
			_context->get_values().insert( make_pair( _str_table->register_str( name ), function.object() ) );
		}

		gc_obj_ptr add( gc_obj_ptr context, gc_obj_ptr_buffer objects )
		{
			cclj_number sum = 0;
			for_each( objects.begin(), objects.end()
					, [&](gc_obj_ptr item) 
					  { 
						  if ( item->user_flags == type_ids::number ) {
							  sum += number_from_gc_object( *item );
						  }
					  } );
			gc_obj_ptr retval( _gc, _gc->allocate( 0, __FILE__, __LINE__ ) );
			retval->user_flags = type_ids::number;
			number_to_gc_object( *retval, sum );
			return retval;
		}
		
	};
}

state_ptr state::create(allocator_ptr alloc ) { return make_shared<state_impl>( alloc ); }