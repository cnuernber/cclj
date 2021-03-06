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

namespace cclj
{
	class module;
}

namespace cclj { namespace plugins {
	typedef llvm::Function* llvm_function_ptr;

	class base_language_plugins
	{
	public:
		base_language_plugins(){}
		ast_node& type_check_symbol( reader_context& context, lisp::symbol& symbol );
		ast_node& type_check_apply( reader_context& context, lisp::cons_cell& cell );
		ast_node& type_check_numeric_constant( reader_context& context, lisp::constant& cell );

		static vector<string> split_symbol(lisp::symbol& sym);

		static void register_base_compiler_plugins( string_table_ptr str_table
													, string_plugin_map_ptr top_level_special_forms
													, string_plugin_map_ptr special_forms
													, string_lisp_evaluator_map& lisp_evaluators );
	};

	//Binary nodes are things that take two arguments and return an answer.
	//Examples of those are things like low level operator + and comparison functions.
	class binary_low_level_ast_node
	{
	public:
		static void register_binary_functions(shared_ptr<module> module, type_library_ptr type_lib, qualified_name_table_ptr name_table);
	};
	
	

}}

#endif