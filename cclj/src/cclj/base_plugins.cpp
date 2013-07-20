//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/plugins/base_plugins.h"
#ifdef _WIN32
#pragma warning(push,2)
#endif
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/Scalar.h"
#ifdef _WIN32
#pragma warning(pop)
#endif

using namespace cclj;
using namespace cclj::lisp;
using namespace cclj::plugins;


pair<ast_node_ptr, type_ref_ptr> apply_plugin::type_check( reader_context& context, lisp::cons_cell& cell )
{
	symbol& fn_name = object_traits::cast_ref<symbol>( cell._value );
	auto plugin_ptr_iter = context._plugins->find( fn_name._name );
	if ( plugin_ptr_iter !=  context._plugins->end() )
	{
		return plugin_ptr_iter->second->type_check( context, cell );
	}
	
	_arg_types.clear();
	_resolved_args.clear();
	//ensure we can find the function definition.
	for ( cons_cell* arg = object_traits::cast<cons_cell>( cell._next )
		; arg; arg = object_traits::cast<cons_cell>( arg->_next ) )
	{
		pair<ast_node_ptr, type_ref_ptr> eval_result = context._type_checker( *arg );
		_arg_types.push_back( eval_result.second );
		_resolved_args.push_back( eval_result.first );
	}
	type_ref& fn_type = context._type_library->get_type_ref( fn_name._name, _arg_types );
	auto context_node_iter = context._symbol_map->find( &fn_type );
	if ( context_node_iter == context._symbol_map->end() ) throw runtime_error( "unable to resolve function" );
	function_def_node& fun_def 
		= compiler_plugin_traits::cast_ref<function_def_node>( context_node_iter->second, context._string_table );
	
	data_buffer<ast_node_ptr> arg_buffer = allocate_buffer<ast_node_ptr>( *context._ast_allocator, _resolved_args );
	function_call_node* new_node = context._ast_allocator->construct<function_call_node>( *this, fun_def, arg_buffer );
	return make_pair( new_node, &fun_def._return_type );
}

void function_call_node::compile_first_pass(compiler_context&) {}


pair<llvm_value_ptr, type_ref_ptr> function_call_node::compile_second_pass(compiler_context& context)
{
	vector<llvm::Value*> fn_args;
	for_each( _arguments.begin(), _arguments.end(), [&]
	( ast_node_ptr node )
	{
		fn_args.push_back( node->compile_second_pass( context ).first );
	} );
	return make_pair( context._builder.CreateCall( _function._function, fn_args, "calltmp" )
					, &_function._return_type );
}