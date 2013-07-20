//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_PLUGINS_BASE_PLUGINS_H
#define CCLJ_PLUGINS_BASE_PLUGINS_H
#pragma once
#include "cclj/cclj.h"
#include "cclj/compiler_plugin.h"

namespace llvm
{
	class Function;
};

namespace cclj { namespace plugins {
	typedef llvm::Function* llvm_function_ptr;
	struct function_call_node;
	struct function_def_node;
	class apply_plugin : public compiler_plugin
	{
		vector<type_ref_ptr> _arg_types;
		vector<ast_node_ptr> _resolved_args;
	public:
		typedef function_call_node ast_node_type;
		static const char* static_plugin_name() { return "apply"; }
		apply_plugin( string_table_ptr table ) 
			: compiler_plugin( table->register_str( static_plugin_name() ) )
		{
		}
		
		virtual pair<ast_node_ptr, type_ref_ptr> type_check( reader_context& context, lisp::cons_cell& cell );
	};

	struct function_call_node : public ast_node
	{
		typedef apply_plugin plugin_type;

		function_def_node&			_function;
		data_buffer<ast_node_ptr>	_arguments;

		function_call_node(const  apply_plugin& plugin, const function_def_node& _fun, data_buffer<ast_node_ptr> args )
			: ast_node( plugin )
			, _function( const_cast<function_def_node&>( _fun ) )
			, _arguments( args )
		{
		}

		virtual void compile_first_pass(compiler_context& context);
		virtual pair<llvm_value_ptr, type_ref_ptr> compile_second_pass(compiler_context& context);
	};

	class function_def_plugin : public compiler_plugin
	{
	public:
		typedef function_def_node ast_node_type;

		static const char* static_plugin_name() { return "function definition"; }
		function_def_plugin( string_table_ptr table )
			: compiler_plugin( table->register_str( static_plugin_name() ) )
		{
		}
		virtual pair<ast_node_ptr, type_ref_ptr> type_check( reader_context& context, lisp::cons_cell& cell );
	};

	struct function_def_node : public ast_node
	{
		typedef function_def_plugin plugin_type;

		type_ref&					_return_type;
		type_ref&					_my_type;
		data_buffer<lisp::symbol*>	_arguments;
		llvm_function_ptr			_function;

		function_def_node( function_def_plugin& plugin, type_ref& rettype, type_ref& mt, data_buffer<lisp::symbol*> arguments )
			: ast_node( plugin )
			, _return_type( rettype )
			, _my_type( mt )
			, _arguments( arguments )
			, _function( nullptr )
		{
		}
		virtual void compile_first_pass(compiler_context& context);
		virtual pair<llvm_value_ptr, type_ref_ptr> compile_second_pass(compiler_context& context);
	};


	template<typename data_type, typename allocator>
	data_buffer<data_type> allocate_buffer( allocator& alloc, data_buffer<data_type> buf )
	{
		if ( buf.size() == 0 ) return data_buffer<data_type>();
			 
		data_type* mem = reinterpret_cast<data_type*>( alloc.allocate( buf.size() * sizeof( data_type )
														, sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() ) );
		memcpy( mem, buf.begin(), buf.size() * sizeof( data_type ) );
		return data_buffer<data_type>( mem, buf.size() );
	}
	

}}

#endif