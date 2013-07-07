//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/state.h"
#include "cclj/allocator.h"
#include "cclj/lisp.h"
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
using namespace llvm;


namespace
{
	
	struct function_def
	{
		symbol*				_name;
		object_ptr_buffer	_arguments;
		cons_cell*			_body;
		Function*			_compiled_code;

		function_def() : _name( nullptr ), _compiled_code( nullptr ) {}
	};

	typedef unordered_map<string_table_str, function_def> function_map;
	typedef unordered_map<string_table_str, AllocaInst*> variable_map;
	typedef unordered_map<string_table_str, Function*> compiler_map;

	struct code_generator;
	typedef object_ptr (*compiler_function)( code_generator*, object_ptr );

	struct code_generator : public noncopyable
	{
		factory_ptr						_factory;
		string_table_ptr				_str_table;
		IRBuilder<>						_builder;
		Module&							_module;
		shared_ptr<ExecutionEngine>		_exec_engine;
		shared_ptr<FunctionPassManager>	_fpm;
		string_table_str				_defn;
		string_table_str				_defmacro;
		string_table_str				_f32;
		string_table_str				_binplus;
		function_map					_context;
		variable_map					_fn_context;
		Function*						_quote_fn;
		Function*						_unquote_fn;
		Function*						_value_fn;
		Function*						_next_fn;
		Function*						_set_value_fn;
		Function*						_set_next_fn;
		Function*						_create_cell_fn;
		FunctionType*					_compiler_fn_type;
		compiler_map					_compiler_functions;

		
		code_generator( factory_ptr f, string_table_ptr str_table, Module& m )
			: _factory( f )
			, _str_table( str_table )
			, _builder( getGlobalContext() )
			, _module( m )
			, _defn( str_table->register_str( "defn" ) )
			, _defmacro( str_table->register_str( "defmacro" ) )
			, _f32( str_table->register_str( "f32" ) )
			, _binplus( str_table->register_str( "+" ) )
			, _compiler_fn_type( nullptr )
		{
			// Create the JIT.  This takes ownership of the module.
			string ErrStr;
			_exec_engine = shared_ptr<ExecutionEngine> ( EngineBuilder(&m).setErrorStr(&ErrStr).create() );
			if (!_exec_engine) {
				throw runtime_error( "Could not create ExecutionEngine\n" );
			}
			_fpm = make_shared<FunctionPassManager>(&m);

			// Set up the optimizer pipeline.  Start with registering info about how the
			// target lays out data structures.
			_fpm->add(new DataLayout(*_exec_engine->getDataLayout()));
			// Provide basic AliasAnalysis support for GVN.
			_fpm->add(createBasicAliasAnalysisPass());
			// Promote allocas to registers.
			_fpm->add(createPromoteMemoryToRegisterPass());
			// Do simple "peephole" optimizations and bit-twiddling optzns.
			_fpm->add(createInstructionCombiningPass());
			// Reassociate expressions.
			_fpm->add(createReassociatePass());
			// Eliminate Common SubExpressions.
			_fpm->add(createGVNPass());
			// Simplify the control flow graph (deleting unreachable blocks, etc).
			_fpm->add(createCFGSimplificationPass());
			_fpm->doInitialization();
			
			vector<Type*> arg_types;
			//runtime
			arg_types.push_back( Type::getInt32PtrTy( getGlobalContext() ) );
			//arg list.
			arg_types.push_back( Type::getInt32PtrTy( getGlobalContext() ) );
			Type* rettype = Type::getInt32PtrTy( getGlobalContext() );
			_compiler_fn_type = FunctionType::get(rettype, arg_types, false );
			_quote_fn = register_compiler_fn( quote, "quote" );
			_unquote_fn = register_compiler_fn( unquote, "unquote" );
			_value_fn = register_compiler_fn( value, "value" );
			_next_fn = register_compiler_fn( next, "next" );
			_set_value_fn = register_compiler_fn( set_value, "set-value" );
			_set_next_fn = register_compiler_fn( set_next, "set-next" );
			_create_cell_fn = register_compiler_fn( create_cell, "create-cell" );
		}

		static object_ptr quote( code_generator* /*rt*/, object_ptr /*arg_list*/ )
		{
			puts( "called!!" );
			return nullptr;
		};

		static object_ptr unquote( code_generator* /*rt*/, object_ptr /*arg_list*/ )
		{
			puts( "called!!" );
			return nullptr;

		}

		static object_ptr value( code_generator* /*rt*/, object_ptr item )
		{
			cons_cell* item_ptr = object_traits::cast<cons_cell>( item );
			if ( item_ptr )
			{
				cons_cell* value_ptr = object_traits::cast<cons_cell>( item_ptr->_value );
				if ( value_ptr )
					return value_ptr->_value;
			}
			return nullptr;
		}

		static object_ptr next( code_generator* /*rt*/, object_ptr item )
		{
			cons_cell* item_ptr = object_traits::cast<cons_cell>( item );
			if ( item_ptr )
			{
				cons_cell* value_ptr = object_traits::cast<cons_cell>( item_ptr->_value );
				if ( value_ptr )
					return value_ptr->_next;
			}
			return nullptr;
		}

		static object_ptr set_next( code_generator* /*rt*/, object_ptr item )
		{
			cons_cell* item_ptr = object_traits::cast<cons_cell>( item );
			if ( item_ptr )
			{
				cons_cell* value_ptr = object_traits::cast<cons_cell>( item_ptr->_value );
				item_ptr = object_traits::cast<cons_cell>( item_ptr->_next );
				if ( value_ptr && item_ptr )
					value_ptr->_next = item_ptr->_value;
			}

			return nullptr;
		}
		static object_ptr set_value( code_generator* /*rt*/, object_ptr item )
		{
			cons_cell* item_ptr = object_traits::cast<cons_cell>( item );
			if ( item_ptr )
			{
				cons_cell* value_ptr = object_traits::cast<cons_cell>( item_ptr->_value );
				item_ptr = object_traits::cast<cons_cell>( item_ptr->_next );
				if ( value_ptr && item_ptr )
					value_ptr->_value = item_ptr->_value;
			}
			return nullptr;
		}

		static object_ptr create_cell( code_generator* rt, object_ptr /*item*/ )
		{
			return rt->_factory->create_cell();
		}

		Function* register_compiler_fn( compiler_function fn, const char* name )
		{
			Function* retval = Function::Create( _compiler_fn_type, Function::ExternalLinkage, "", &_module );
			_exec_engine->addGlobalMapping( retval, reinterpret_cast<void*>( fn ) );
			_compiler_functions.insert( make_pair( _str_table->register_str( name ), retval ) );
			return retval;
		}


		Type* symbol_type( object_ptr obj )
		{
			symbol& sym = object_traits::cast<symbol>( *obj );
			if ( sym._type && sym._type->_name == _f32 )
				return Type::getFloatTy( getGlobalContext() );
			throw runtime_error( "Unrecoginzed symbol type" );
		}

		string_table_str symbol_name( object_ptr obj )
		{
			symbol& sym = object_traits::cast<symbol>( *obj );
			return sym._name;
		}

		Value* codegen_apply( cons_cell& cell )
		{
			symbol& fn_name = object_traits::cast<symbol>( *cell._value );

			vector<Value*> fn_args;
			for ( cons_cell * arg_cell = object_traits::cast<cons_cell>( cell._next )
				; arg_cell; arg_cell = object_traits::cast<cons_cell>( arg_cell->_next ) )
			{
				Value* val = codegen_expr( arg_cell->_value );
				if( val == nullptr ) throw runtime_error( "statement eval failed" );
				fn_args.push_back( val );
			}

			if ( fn_name._name == _binplus )
			{
				if ( fn_args.size() != 2 )
					throw runtime_error( "Unexpected num args" );
				return _builder.CreateFAdd( fn_args[0], fn_args[1], "addtmp" );
			}

			function_map::iterator iter = _context.find( fn_name._name );

			if ( iter == _context.end() ) throw runtime_error( "Failed to find function" );
			if ( iter->second._compiled_code == nullptr ) throw runtime_error( "null function" );
			if ( iter->second._compiled_code->arg_size() != fn_args.size() ) 
				throw runtime_error( "function arity mismatch" );

			return _builder.CreateCall( iter->second._compiled_code, fn_args, "calltmp" );
		}
		Value* codegen_var( symbol& symbol )
		{
			variable_map::iterator iter = _fn_context.find( symbol._name );
			if ( iter == _fn_context.end() ) throw runtime_error( "failed to lookup variable" );
			return _builder.CreateLoad( iter->second, symbol._name.c_str() );
		}
		Value* codegen_constant( constant& data )
		{
			return ConstantFP::get(getGlobalContext(), APFloat(data.value) );
		}

		Value* codegen_expr( object_ptr expr )
		{
			switch( expr->type() )
			{
			case types::cons_cell:
				return codegen_apply( object_traits::cast<cons_cell>( *expr ) );
			case types::symbol:
				return codegen_var( object_traits::cast<symbol>( *expr ) );
			case types::constant:
				return codegen_constant( object_traits::cast<constant>( *expr ) );
			default:
				throw runtime_error( "unrecognized top level type" );
			}
		}

		void codegen_function_body( Function* fn, cons_cell& fn_body )
		{
			//codegen the body of the function
			for ( cons_cell* progn_cell = &fn_body; progn_cell
						; progn_cell = object_traits::cast<cons_cell>( progn_cell->_next ) )
			{
				bool is_last = progn_cell->_next == nullptr;
				Value* last_statement_return = codegen_expr( progn_cell->_value );
				if ( is_last )
				{
					if ( last_statement_return )
					{
						_builder.CreateRet( last_statement_return );
						verifyFunction(*fn );
						_fpm->run( *fn );
					}
					else
						throw runtime_error( "function definition failed" );
				}
			}
		}
		
		Function* codegen_function_def( cons_cell& defn_cell )
		{
			//use dereference cast because we want an exception if it isn't a cons cell.
			cons_cell* item = &object_traits::cast<cons_cell>( *defn_cell._next );
			symbol& fn_def = object_traits::cast<symbol>( *item->_value );
			item = &object_traits::cast<cons_cell>( *item->_next );
			array& fn_args = object_traits::cast<array>( *item->_value );
			item = &object_traits::cast<cons_cell>( *item->_next );
			cons_cell& fn_body = *item;
			if ( item->_next != nullptr )
				throw runtime_error( "failed to handle function nullptr" );

			function_def* fn_entry = nullptr;
			if ( fn_def._name.empty() == false )
			{
				pair<function_map::iterator, bool> inserter = _context.insert( make_pair( fn_def._name, function_def() ) );
				fn_entry = &inserter.first->second;
				//erase first function so we can make new function
				if ( inserter.second == false && fn_entry->_compiled_code )
				{
					fn_entry->_compiled_code->eraseFromParent();
					fn_entry->_compiled_code = nullptr;
				}

				fn_entry->_name = &fn_def;
				fn_entry->_arguments = fn_args._data;
				fn_entry->_body = &fn_body;
			}
			FunctionType* fn_type = nullptr;
			vector<Type*> arg_types;
			for_each( fn_args._data.begin(), fn_args._data.end(), [&]
			(object_ptr arg)
			{
				symbol& sym = object_traits::cast<symbol>(*arg);
				if ( sym._type == nullptr ) throw runtime_error( "failed to handle symbol with no type" );
				if ( sym._type->_name != _f32 ) throw runtime_error( "unrecogized data type" );
				arg_types.push_back( Type::getFloatTy(getGlobalContext()));
			} );

			if ( fn_def._type == nullptr ) throw runtime_error( "unrecoginzed function return type" );
			if ( fn_def._type->_name != _f32 ) throw runtime_error( "unrecoginzed function return type" );

			Type* rettype = Type::getFloatTy( getGlobalContext() );
			fn_type = FunctionType::get(rettype, arg_types, false);
			Function* fn = Function::Create( fn_type, Function::ExternalLinkage, fn_def._name.c_str(), &_module );
			
			if ( fn_entry )
				fn_entry->_compiled_code = fn;

			size_t arg_idx = 0;
			for (Function::arg_iterator AI = fn->arg_begin(); arg_idx != fn_args._data.size();
				++AI, ++arg_idx)
			{
				symbol& fn_arg = object_traits::cast<symbol>( *fn_args._data[arg_idx] );
				AI->setName(fn_arg._name.c_str());
			}
			
			// Create a new basic block to start insertion into.
			BasicBlock *function_entry_block = BasicBlock::Create(getGlobalContext(), "entry", fn);
			_builder.SetInsertPoint(function_entry_block);

			IRBuilder<> entry_block_builder( &fn->getEntryBlock(), fn->getEntryBlock().begin() );
			
			Function::arg_iterator AI = fn->arg_begin();
			for (unsigned Idx = 0, e = fn_args._data.size(); Idx != e; ++Idx, ++AI) {
				// Create an alloca for this variable.
				AllocaInst *Alloca = entry_block_builder.CreateAlloca(symbol_type( fn_args._data[Idx] ), 0,
						symbol_name(fn_args._data[Idx]).c_str());

				// Store the initial value into the alloca.
				_builder.CreateStore(AI, Alloca);
				
				bool inserted = _fn_context.insert(make_pair( symbol_name(fn_args._data[Idx]), Alloca ) ).second;
				if ( !inserted )
					throw runtime_error( "duplicate function argument name" );
			}
			codegen_function_body( fn, fn_body );
			_fn_context.clear();
			return fn;
		}
	};

	typedef float (*anon_fn_type)();

	struct state_impl : public state
	{
		allocator_ptr		_alloc;
		string_table_ptr	_str_table;
		cons_cell			_empty_cell;

		shared_ptr<code_generator>				_code_gen;


		state_impl() : _alloc( allocator::create_checking_allocator() )
			, _str_table( string_table::create() )
		{
		}
		virtual float execute( const string& data )
		{
			factory_ptr factory = factory::create_factory( _alloc, _empty_cell );
			reader_ptr reader = reader::create_reader( factory, _str_table );
			object_ptr_buffer parse_result = reader->parse( data );
			//run through, code-gen the function defs and call the functions.
			InitializeNativeTarget();
			LLVMContext &Context = getGlobalContext();
			Module* module( new Module("my cool jit", Context) );
			_code_gen = make_shared<code_generator>( factory, _str_table, *module );

			float retval = 0.0f;
			for( object_ptr* obj_iter = parse_result.begin(), *end = parse_result.end();
				obj_iter != end; ++obj_iter )
			{
				object_ptr obj = *obj_iter;
				cons_cell* cell = object_traits::cast<cons_cell>( obj );
				if ( cell )
				{
					symbol* item_name = object_traits::cast<symbol>(cell->_value);
					if ( item_name )
					{
						if ( item_name->_name == _code_gen->_defn )
						{
							//run codegen for function.
							_code_gen->codegen_function_def( *cell );
						}
						else
						{
							Type* rettype = Type::getFloatTy( getGlobalContext() );
							FunctionType* fn_type = FunctionType::get(rettype, vector<Type*>(), false);
							Function* fn = Function::Create( fn_type
																, Function::ExternalLinkage
																, "", &_code_gen->_module );
							
							BasicBlock *function_entry_block = BasicBlock::Create(getGlobalContext(), "entry", fn);
							_code_gen->_builder.SetInsertPoint(function_entry_block);
							cons_cell* progn_cell = factory->create_cell();
							progn_cell->_value = cell;
							_code_gen->codegen_function_body( fn, *progn_cell );
							
							void *fn_ptr= _code_gen->_exec_engine->getPointerToFunction(fn);
							if ( !fn_ptr ) throw runtime_error( "function definition failed" );
							anon_fn_type anon_fn = reinterpret_cast<anon_fn_type>( fn_ptr );
							retval = anon_fn();
						}
					}
				}
			}

			return retval;
		}
	};
}

state_ptr state::create_state() { return make_shared<state_impl>(); }


