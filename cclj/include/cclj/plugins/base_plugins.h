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
#include "cclj/plugins/compiler_plugin.h"

namespace llvm
{
	class Function;
};

namespace cclj { namespace plugins {
	typedef llvm::Function* llvm_function_ptr;
	struct function_call_node;
	struct function_def_node;

	class base_language_plugins
	{
		vector<type_ref_ptr> _arg_types;
		vector<ast_node_ptr> _resolved_args;
	public:
		base_language_plugins(){}
		ast_node& type_check_apply( reader_context& context, lisp::cons_cell& cell );
		ast_node& type_check_symbol( reader_context& context, lisp::cons_cell& cell );
		ast_node& type_check_numeric_constant( reader_context& context, lisp::cons_cell& cell );
	};

	struct function_call_node : public ast_node
	{
		function_def_node&			_function;

		static const char* static_node_type() { return "function call"; }

		function_call_node(string_table_ptr str_table, const function_def_node& _fun );

		virtual bool executable_statement() const { return true; }
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
		virtual ast_node& type_check( reader_context& context, lisp::cons_cell& cell );
	};

	struct function_def_node : public ast_node
	{
		static const char* static_node_type() { return "function definition"; }

		lisp::symbol&				_name;
		type_ref&					_my_type;
		data_buffer<lisp::symbol*>	_arguments;
		llvm_function_ptr			_function;

		function_def_node( string_table_ptr str_table, const lisp::symbol& name
							, const type_ref& mt, data_buffer<lisp::symbol*> arguments )
			: ast_node( str_table->register_str( static_node_type() ), *name._type )
			, _name( const_cast<lisp::symbol&>( name ) )
			, _my_type( const_cast<type_ref&>( mt ) )
			, _arguments( arguments )
			, _function( nullptr )
		{
		}
		virtual ast_node& apply( reader_context& context, data_buffer<ast_node_ptr> args );
		virtual void compile_first_pass(compiler_context& context);
		virtual pair<llvm_value_ptr, type_ref_ptr> compile_second_pass(compiler_context& context);

		static void initialize_function(compiler_context& context, llvm::Function& fn
			, data_buffer<lisp::symbol*> fn_args, variable_context& var_context );
	};

	typedef function<void( type_ref& fn_type, ast_node& comp_node )> register_function;

	//Binary nodes are things that take two arguments and return an answer.
	//Examples of those are things like low level operator + and comparison functions.
	class binary_low_level_ast_node
	{
	public:
		static void register_binary_functions( register_function fn, type_library_ptr type_lib
			, string_table_ptr str_table, slab_allocator_ptr ast_allocator );
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