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
using namespace llvm;


ast_node& apply_plugin::type_check( reader_context& context, lisp::cons_cell& cell )
{
	symbol& fn_name = object_traits::cast_ref<symbol>( cell._value );
	auto plugin_ptr_iter = context._special_forms->find( fn_name._name );
	if ( plugin_ptr_iter !=  context._special_forms->end() )
	{
		return plugin_ptr_iter->second->type_check( context, cell );
	}
	
	_arg_types.clear();
	_resolved_args.clear();
	//ensure we can find the function definition.
	for ( cons_cell* arg = object_traits::cast<cons_cell>( cell._next )
		; arg; arg = object_traits::cast<cons_cell>( arg->_next ) )
	{
		ast_node& eval_result = context._type_checker( *arg );
		_arg_types.push_back( &eval_result.type() );
		_resolved_args.push_back( &eval_result );
	}
	type_ref& fn_type = context._type_library->get_type_ref( fn_name._name, _arg_types );
	auto context_node_iter = context._symbol_map->find( &fn_type );
	if ( context_node_iter == context._symbol_map->end() ) throw runtime_error( "unable to resolve function" );
	ast_node& new_node = context_node_iter->second->apply( context, _resolved_args );
	return new_node;
}

function_call_node::function_call_node(string_table_ptr str_table, const function_def_node& _fun )
	: ast_node( str_table->register_str( static_node_type() ), *_fun._name._type )
	, _function( const_cast<function_def_node&>( _fun ) )
{
}


pair<llvm_value_ptr, type_ref_ptr> function_call_node::compile_second_pass(compiler_context& context)
{
	vector<llvm::Value*> fn_args;
	for_each( children().begin(), children().end(), [&]
	( ast_node_ptr node )
	{
		fn_args.push_back( node->compile_second_pass( context ).first );
	} );
	return make_pair( context._builder.CreateCall( _function._function, fn_args, "calltmp" )
					, _function._name._type );
}



ast_node& function_def_plugin::type_check( reader_context& context, lisp::cons_cell& cell )
{
	cons_cell& fn_name_cell = object_traits::cast_ref<cons_cell>( cell._next );
	symbol& fn_name = object_traits::cast_ref<symbol>( fn_name_cell._value );
	if ( fn_name._type == nullptr ) throw runtime_error( "function return type not specified" );
	cons_cell& arg_array_cell = object_traits::cast_ref<cons_cell>( fn_name_cell._next );
	array&  arg_array = object_traits::cast_ref<array>( arg_array_cell._value );
	cons_cell& body = object_traits::cast_ref<cons_cell>( arg_array_cell._next );
	symbol_type_context symbol_context( context._context_symbol_types );
	vector<type_ref_ptr> type_array;
	vector<symbol*> symbol_array;
	for ( size_t idx = 0, end = arg_array._data.size(); idx < end; ++idx )
	{
		symbol& arg_symbol = object_traits::cast_ref<symbol>( arg_array._data[idx] );
		if ( arg_symbol._type == nullptr ) throw runtime_error( "function arguments must have types" );
		type_array.push_back( arg_symbol._type );
		symbol_array.push_back( &arg_symbol );
		symbol_context.add_symbol( arg_symbol._name, *arg_symbol._type );
	}
	type_ref& fn_type = context._type_library->get_type_ref( fn_name._name, type_array );
	data_buffer<symbol*> args = allocate_buffer<symbol*>( *context._ast_allocator, symbol_array );
	function_def_node* new_node = context._ast_allocator->construct<function_def_node>( *this, fn_name, fn_type, args );
	ast_node_ptr last_body_eval( nullptr);
	for ( cons_cell* body_cell = &body; body_cell; body_cell = object_traits::cast<cons_cell>( body_cell->_next ) )
	{
		last_body_eval = &context._type_checker( *body_cell );
		new_node->children().push_back( *last_body_eval );
	}
	if ( &last_body_eval->type() != fn_name._type ) 
		throw runtime_error( "function return type does not equal last statement" );
	return *new_node;
}


ast_node& function_def_node::apply( reader_context& context, data_buffer<ast_node_ptr> args )
{
	function_call_node* new_node = context._ast_allocator->construct<function_call_node>( context._string_table, *this );
	for_each( args.begin(), args.end(), [&]
	( ast_node_ptr arg )
	{
		new_node->children().push_back( *arg );
	} );
	return *new_node;
}


void function_def_node::compile_first_pass(compiler_context& context)
{
	vector<llvm_type_ptr> arg_types;
	for_each( _arguments.begin(), _arguments.end(), [&]
	(symbol* sym )
	{
		arg_types.push_back( context.type_ref_type( *sym->_type ) );
	} );
	llvm_type_ptr rettype = context.type_ref_type( *_name._type );
	FunctionType* fn_type = FunctionType::get(rettype, arg_types, false);
	_function = Function::Create( fn_type, Function::ExternalLinkage, _name._name.c_str(), &context._module );
}

pair<llvm_value_ptr, type_ref_ptr> function_def_node::compile_second_pass(compiler_context& context)
{
	variable_context fn_context( context._variables );
	initialize_function( context, *_function, _arguments, fn_context );
	pair<llvm_value_ptr, type_ref_ptr> last_statement( nullptr, nullptr );
	for ( auto iter = children().begin(), end = children().end(); iter != end; ++iter )
	{
		last_statement = iter->compile_second_pass( context );
	}
	if ( last_statement.first == nullptr ) throw runtime_error( "function definition failed" );
	Value* retVal = context._builder.CreateRet( last_statement.first );
	verifyFunction(*_function );
	context._fpm.run( *_function );
	return make_pair( retVal, _name._type );
}


void function_def_node::initialize_function(compiler_context& context, Function& fn, data_buffer<symbol*> fn_args
												, variable_context& var_context)
{
	size_t arg_idx = 0;
	for (Function::arg_iterator AI = fn.arg_begin(); arg_idx !=fn_args.size();
		++AI, ++arg_idx)
	{
		symbol& fn_arg = *fn_args[arg_idx];
		AI->setName(fn_arg._name.c_str());
	}
			
	// Create a new basic block to start insertion into.
	BasicBlock *function_entry_block = BasicBlock::Create(getGlobalContext(), "entry", &fn);
	context._builder.SetInsertPoint(function_entry_block);
	Function::arg_iterator AI = fn.arg_begin();
	IRBuilder<> entry_block_builder( &fn.getEntryBlock(), fn.getEntryBlock().begin() );
	for (unsigned Idx = 0, e = fn_args.size(); Idx != e; ++Idx, ++AI) {
		symbol& arg_def( *fn_args[Idx] );
		// Create an alloca for this variable.
		if ( arg_def._type == nullptr ) throw runtime_error( "Invalid function argument" );
		AllocaInst *Alloca = entry_block_builder.CreateAlloca(context.type_ref_type( *arg_def._type ), 0,
				arg_def._name.c_str());

		// Store the initial value into the alloca.
		context._builder.CreateStore(AI, Alloca);
		var_context.add_variable( arg_def._name, Alloca, *arg_def._type );
	}
}

namespace 
{

	typedef function<llvm_value_ptr (IRBuilder<>& builder, Value* lhs, Value* rhs )> builder_binary_function;

	//this node is both registered as a global symbol and used at the callsite.
	struct binary_ast_node : public ast_node
	{
		builder_binary_function _function;
		binary_ast_node( string_table_str name, type_ref& rettype, builder_binary_function build_fn )
			: ast_node( name, rettype )
			, _function( build_fn )
		{
		}

		
		virtual ast_node& apply( reader_context& context, data_buffer<ast_node_ptr> args )
		{
			binary_ast_node* retval = context._ast_allocator->construct<binary_ast_node>( node_type(), type() );
			retval->children().push_back( *args[0] );
			retval->children().push_back( *args[1] );
			return *retval;
		}

		virtual bool executable_statement() const { return true; }
		
		virtual pair<llvm_value_ptr, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			ast_node& lhs = *children()._head;
			ast_node& rhs = *lhs.next_node();
			llvm_value_ptr lhs_value = lhs.compile_second_pass( context ).first;
			llvm_value_ptr rhs_value = rhs.compile_second_pass( context ).first;
			llvm_value_ptr retval = _function( context._builder, lhs_value, rhs_value );
			return make_pair( retval, &type() );
		}
	};


	void do_register_binary_fn( register_function fn
									, string_table_str node_name
									, string_table_str symbol_name
									, type_library_ptr type_lib
									, builder_binary_function builder_function
									, base_numeric_types::_enum in_arg_type
									, base_numeric_types::_enum in_ret_type
									, slab_allocator_ptr ast_allocator )
	{
		type_ref& ret_type = type_lib->get_type_ref( in_ret_type );
		type_ref& arg_type = type_lib->get_type_ref( in_arg_type );
		binary_ast_node* new_node = ast_allocator->construct<binary_ast_node>( node_name, ret_type, builder_function );
		type_ref* arg_array[2] = { &ret_type, &ret_type };
		type_ref& fn_type = type_lib->get_type_ref( symbol_name, data_buffer<type_ref_ptr>( arg_array, 2 ) );
		fn( fn_type, *new_node );
	}


	void register_binary_float_fn( register_function fn, string_table_str n, type_library_ptr type_lib
									, builder_binary_function builder_function
									, string_table_str fn_symbol
									, slab_allocator_ptr ast_allocator
									, bool ret_type_same_as_arg_type )
	{
		do_register_binary_fn( fn, n, fn_symbol, type_lib, builder_function
			, base_numeric_types::f32, ret_type_same_as_arg_type ? base_numeric_types::f32 : base_numeric_types::i1
			, ast_allocator );
		do_register_binary_fn( fn, n, fn_symbol, type_lib, builder_function
			, base_numeric_types::f64, ret_type_same_as_arg_type ? base_numeric_types::f64 : base_numeric_types::i1
			, ast_allocator );
	}
	
	void register_binary_signed_integer_fn( register_function fn, string_table_str n, type_library_ptr type_lib
									, builder_binary_function builder_function
									, string_table_str fn_symbol
									, slab_allocator_ptr ast_allocator
									, bool ret_type_same_as_arg_type )
	{
		do_register_binary_fn( fn, n, fn_symbol, type_lib, builder_function
			, base_numeric_types::i8, ret_type_same_as_arg_type ? base_numeric_types::i8 : base_numeric_types::i1
			, ast_allocator );
		do_register_binary_fn( fn, n, fn_symbol, type_lib, builder_function
			, base_numeric_types::i16, ret_type_same_as_arg_type ? base_numeric_types::i16 : base_numeric_types::i1
			, ast_allocator );
		do_register_binary_fn( fn, n, fn_symbol, type_lib, builder_function
			, base_numeric_types::i32, ret_type_same_as_arg_type ? base_numeric_types::i32 : base_numeric_types::i1
			, ast_allocator );
		do_register_binary_fn( fn, n, fn_symbol, type_lib, builder_function
			, base_numeric_types::i64, ret_type_same_as_arg_type ? base_numeric_types::i64 : base_numeric_types::i1
			, ast_allocator );
	}
	
	void register_binary_unsigned_integer_fn( register_function fn, string_table_str n, type_library_ptr type_lib
												, builder_binary_function builder_function
												, string_table_str fn_symbol
												, slab_allocator_ptr ast_allocator
												, bool ret_type_same_as_arg_type)
	{
		do_register_binary_fn( fn, n, fn_symbol, type_lib, builder_function
			, base_numeric_types::u8, ret_type_same_as_arg_type ? base_numeric_types::u8 : base_numeric_types::i1
			, ast_allocator );
		do_register_binary_fn( fn, n, fn_symbol, type_lib, builder_function
			, base_numeric_types::u16, ret_type_same_as_arg_type ? base_numeric_types::u16 : base_numeric_types::i1
			, ast_allocator );
		do_register_binary_fn( fn, n, fn_symbol, type_lib, builder_function
			, base_numeric_types::u32, ret_type_same_as_arg_type ? base_numeric_types::u32 : base_numeric_types::i1
			, ast_allocator );
		do_register_binary_fn( fn, n, fn_symbol, type_lib, builder_function
			, base_numeric_types::u64, ret_type_same_as_arg_type ? base_numeric_types::u64 : base_numeric_types::i1
			, ast_allocator );
	}
	
	void register_binary_integer_fn( register_function fn, string_table_str n, type_library_ptr type_lib
									, builder_binary_function builder_function
									, string_table_str fn_symbol
									, slab_allocator_ptr ast_allocator
									, bool ret_type_same_as_arg_type)
	{
		register_binary_signed_integer_fn( fn, n, type_lib, builder_function, fn_symbol
												, ast_allocator, ret_type_same_as_arg_type );
		register_binary_unsigned_integer_fn( fn, n, type_lib, builder_function, fn_symbol
												, ast_allocator, ret_type_same_as_arg_type );
	}
}




void binary_low_level_ast_node::register_binary_functions( register_function fn, type_library_ptr type_lib
																, string_table_ptr str_table
																, slab_allocator_ptr ast_allocator)
{
	register_binary_float_fn( fn, str_table->register_str( "float plus" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateFAdd( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "+" )
								, ast_allocator, true);

	register_binary_float_fn( fn, str_table->register_str( "float minus" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateFSub( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "-" )
								, ast_allocator, true );
	
	register_binary_float_fn( fn, str_table->register_str( "float multiply" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateFMul( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "*" )
								, ast_allocator, true );
	
	register_binary_float_fn( fn, str_table->register_str( "float divide" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateFDiv( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "/" )
								, ast_allocator, true );
	
	register_binary_integer_fn( fn, str_table->register_str( "int plus" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateAdd( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "+" )
								, ast_allocator, true );
	
	register_binary_integer_fn( fn, str_table->register_str( "int minus" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateSub( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "-" )
								, ast_allocator, true );
	
	register_binary_integer_fn( fn, str_table->register_str( "int multiply" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateMul( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "*" )
								, ast_allocator, true );
	
	register_binary_signed_integer_fn( fn, str_table->register_str( "int divide" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateSDiv( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "/" )
								, ast_allocator, true );
	
	register_binary_unsigned_integer_fn( fn, str_table->register_str( "int divide" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateUDiv( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "/" )
								, ast_allocator, true );
}
