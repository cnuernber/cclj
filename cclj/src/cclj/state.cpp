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
#include "cclj/llvm_base_numeric_type_helper.h"


using namespace cclj;
using namespace cclj::lisp;
using namespace llvm;


namespace
{
	
	typedef function<Value*(const vector<Value*>&)> compiler_intrinsic;
	struct function_def
	{
		symbol*				_name;
		object_ptr_buffer	_arguments;
		cons_cell*			_body;
		Function*			_compiled_code;
		compiler_intrinsic	_compiler_code;
		

		function_def() : _name( nullptr ), _compiled_code( nullptr ) {}
	};

	typedef function<pair<Value*, type_ref_ptr> (cons_cell& cell)> compiler_special_form;

	typedef unordered_map<type_ref_ptr, function_def> function_map;
	typedef unordered_map<string_table_str, AllocaInst*> variable_map;
	typedef unordered_map<string_table_str, Function*> compiler_map;
	typedef unordered_map<string_table_str, compiler_special_form> compiler_special_form_map;

	struct code_generator;
	typedef object_ptr (*compiler_function)( code_generator*, object_ptr );


	struct code_generator : public noncopyable
	{
		factory_ptr						_factory;
		type_system_ptr					_type_system;
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
		compiler_special_form_map		_special_forms;

		
		code_generator( factory_ptr f, type_system_ptr t, string_table_ptr str_table, Module& m )
			: _factory( f )
			, _type_system( t )
			, _str_table( str_table )
			, _builder( getGlobalContext() )
			, _module( m )
			, _defn( str_table->register_str( "defn" ) )
			, _defmacro( str_table->register_str( "defmacro" ) )
			, _f32( str_table->register_str( "f32" ) )
			, _binplus( str_table->register_str( "+" ) )
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
			register_compiler_binary_fn( bind( &code_generator::f_plus, this, std::placeholders::_1 ), "+", "f32" );
			register_compiler_binary_fn( bind( &code_generator::f_minus, this, std::placeholders::_1 ), "-", "f32" );
			register_compiler_binary_fn( bind( &code_generator::f_mult, this, std::placeholders::_1 ), "*", "f32" );
			register_compiler_compare_fn( bind( &code_generator::f_less_than, this, std::placeholders::_1 ), "<", "f32" );
			register_compiler_compare_fn( bind( &code_generator::f_greater_than, this, std::placeholders::_1 ), ">", "f32" );
			register_special_form( bind( &code_generator::if_special_form, this, std::placeholders::_1 ), "if" );
			register_special_form( bind( &code_generator::let_special_form, this, std::placeholders::_1 ), "let" );
		}

		Type* symbol_type( object_ptr obj )
		{
			symbol& sym = object_traits::cast<symbol>( *obj );
			if ( sym._type && sym._type )
				return type_ref_type( *sym._type );
			throw runtime_error( "Unrecoginzed symbol type" );
		}

		Type* type_ref_type( type_ref& type )
		{
			if ( &type == &_type_system->get_type_ref( _str_table->register_str( "f32" ) ) )
				return Type::getFloatTy( getGlobalContext() );
			else if ( &type == &_type_system->get_type_ref( _str_table->register_str( "i1" ) ) )
				return Type::getInt1Ty( getGlobalContext() );
			throw runtime_error( "Unrecoginzed type" );
		}

		string_table_str symbol_name( object_ptr obj )
		{
			symbol& sym = object_traits::cast<symbol>( *obj );
			return sym._name;
		}

		Value* f_plus( const vector<Value*>& args )
		{
			return _builder.CreateFAdd( args[0], args[1], "tmpadd" );
		}

		Value* f_minus( const vector<Value*>& args )
		{
			return _builder.CreateFSub( args[0], args[1], "tmpsub" );
		}

		Value* f_mult( const vector<Value*>& args )
		{
			return _builder.CreateFMul( args[0], args[1], "tmpmul" );
		}

		Value* f_less_than( const vector<Value*>& args )
		{
			return _builder.CreateFCmpULT( args[0], args[1], "tmpcmp" );
		}

		Value* f_greater_than( const vector<Value*>& args )
		{
			return _builder.CreateFCmpUGT( args[0], args[1], "tmpcmp" );
		}

		pair<Value*, type_ref_ptr> let_special_form( cons_cell& cell )
		{
			cons_cell& var_assign = object_traits::cast_ref<cons_cell>( cell._next );
			cons_cell& body = object_traits::cast_ref<cons_cell>( var_assign._next );
			object_ptr_buffer assign_block = object_traits::cast_ref<array>( var_assign._value )._data;
			typedef pair<string_table_str, AllocaInst*> var_entry; 
			vector<var_entry> shadowed_vars;
			if ( assign_block.size() % 2 ) throw runtime_error( "invalid let statement" );
			//Create builder that inserts to the function entry block.
			Function* theFunction = _builder.GetInsertBlock()->getParent();
			IRBuilder<> entryBuilder( &theFunction->getEntryBlock(), theFunction->getEntryBlock().begin() );
			for ( size_t idx = 0, end = assign_block.size(); idx < end; idx += 2 )
			{
				symbol& name = object_traits::cast_ref<symbol>( assign_block[idx] );
				pair<Value*, type_ref_ptr> expr_result = codegen_expr( assign_block[idx+1] );
				AllocaInst* data = entryBuilder.CreateAlloca( type_ref_type( *expr_result.second ), 0, name._name.c_str() );
				variable_map::iterator iter = _fn_context.find( name._name );
				if( iter != _fn_context.end() )
					shadowed_vars.push_back( *iter );
				//Immediately map the name so it can be used in the very next expr
				_fn_context[name._name] = data;
				_builder.CreateStore( expr_result.first, data );
			}
			pair<Value*, type_ref_ptr> retval = codegen_expr( body._value );
			for_each( shadowed_vars.begin(), shadowed_vars.end(), [this]( const var_entry& entry )
			{
				//Reset the variable context.
				_fn_context[entry.first] = entry.second;
			} );
			return retval;
		}

		pair<Value*, type_ref_ptr> if_special_form( cons_cell& cell )
		{
			cons_cell* condition = object_traits::cast<cons_cell>( cell._next );
			if ( !condition ) throw runtime_error( "invalid if statement" );
			cons_cell* then_statement = object_traits::cast<cons_cell>( condition->_next );
			if ( !then_statement ) throw runtime_error( "invalid if statement" );
			cons_cell* else_statement = object_traits::cast<cons_cell>( then_statement->_next );
			if ( !else_statement ) throw runtime_error( "invalid if statement" );

			pair<Value*,type_ref_ptr> condexpr = codegen_expr( condition->_value );
			type_ref& bool_type = _type_system->get_type_ref( _str_table->register_str( "i1" ) );
			if ( condexpr.second != &bool_type ) throw runtime_error( "if statements only work on boolean exprs" );
			
			Function *theFunction = _builder.GetInsertBlock()->getParent();
  
			// Create blocks for the then and else cases.  Insert the 'then' block at the
			// end of the function.
			// This code is taken directly from the tutorial.  I do not full understand it.
			BasicBlock *ThenBB = BasicBlock::Create(getGlobalContext(), "then", theFunction);

			BasicBlock *ElseBB = BasicBlock::Create(getGlobalContext(), "else");

			BasicBlock *MergeBB = BasicBlock::Create(getGlobalContext(), "ifcont");
			_builder.CreateCondBr( condexpr.first, ThenBB, ElseBB );
			_builder.SetInsertPoint( ThenBB );

			pair<Value*,type_ref_ptr> thenExpr = codegen_expr( then_statement->_value );

			_builder.CreateBr( MergeBB );

			ThenBB = _builder.GetInsertBlock();

			theFunction->getBasicBlockList().push_back( ElseBB );
			_builder.SetInsertPoint( ElseBB );

			pair<Value*,type_ref_ptr> elseExpr = codegen_expr( else_statement->_value );

			if ( thenExpr.second != elseExpr.second ) 
				throw runtime_error( "Invalid if statement, expressions do not match type" );

			_builder.CreateBr( MergeBB );

			ElseBB = _builder.GetInsertBlock();

			
			// Emit merge block.
			theFunction->getBasicBlockList().push_back(MergeBB);
			_builder.SetInsertPoint(MergeBB);
			PHINode *PN = _builder.CreatePHI( type_ref_type(*thenExpr.second ), 2,
											"iftmp");
  
			PN->addIncoming(thenExpr.first, ThenBB);
			PN->addIncoming(elseExpr.first, ElseBB);
			return make_pair(PN, thenExpr.second);
		}

		void register_special_form( compiler_special_form fn, const char* name )
		{
			_special_forms.insert( make_pair( _str_table->register_str( name ), fn ) );
		}

		//binary functions take two arguments and return the the same type.
		void register_compiler_binary_fn( compiler_intrinsic fn, const char* name, const char* type )
		{
			type_ref& arg_type = _type_system->get_type_ref( _str_table->register_str( type ), type_ref_ptr_buffer() );
			type_ref* buffer[2] = { &arg_type, &arg_type};
			type_ref& func_type = _type_system->get_type_ref( _str_table->register_str( name), type_ref_ptr_buffer( buffer, 2 ) );
			function_def theDef;
			theDef._name = _factory->create_symbol();
			theDef._name->_name = _str_table->register_str( name );
			theDef._name->_type = &arg_type;
			theDef._body = nullptr;
			theDef._compiler_code = fn;
			_context.insert( make_pair( &func_type, theDef ) );
		}
		//comparison functions take two arguments and return an i1.
		void register_compiler_compare_fn( compiler_intrinsic fn, const char* name, const char* type )
		{
			type_ref& arg_type = _type_system->get_type_ref( _str_table->register_str( type ), type_ref_ptr_buffer() );
			type_ref& ret_type = _type_system->get_type_ref( _str_table->register_str( "i1" ), type_ref_ptr_buffer() );
			type_ref* buffer[2] = { &arg_type, &arg_type};
			type_ref& func_type = _type_system->get_type_ref( _str_table->register_str( name), type_ref_ptr_buffer( buffer, 2 ) );
			function_def theDef;
			theDef._name = _factory->create_symbol();
			theDef._name->_name = _str_table->register_str( name );
			theDef._name->_type = &ret_type;
			theDef._body = nullptr;
			theDef._compiler_code = fn;
			_context.insert( make_pair( &func_type, theDef ) );
		}

		pair<Value*,type_ref_ptr> codegen_apply( cons_cell& cell )
		{
			symbol& fn_name = object_traits::cast_ref<symbol>( cell._value );

			//check special forms.  If a special form takes this then jump there.
			compiler_special_form_map::iterator form_iter = _special_forms.find( fn_name._name );
			if ( form_iter != _special_forms.end() )
				return form_iter->second( cell );

			vector<Value*> fn_args;
			vector<type_ref_ptr> fn_arg_types;
			for ( cons_cell * arg_cell = object_traits::cast<cons_cell>( cell._next )
				; arg_cell; arg_cell = object_traits::cast<cons_cell>( arg_cell->_next ) )
			{
				pair<Value*,type_ref_ptr> val = codegen_expr( arg_cell->_value );
				if( val.first == nullptr ) throw runtime_error( "statement eval failed" );
				fn_args.push_back( val.first );
				fn_arg_types.push_back( val.second );
			}

			type_ref& fn_type = _type_system->get_type_ref( fn_name._name, fn_arg_types );

			function_map::iterator iter = _context.find( &fn_type );

			if ( iter == _context.end() ) throw runtime_error( "Failed to find function" );
			if ( iter->second._compiled_code == nullptr && iter->second._compiler_code._Empty() ) throw runtime_error( "null function" );
			if ( iter->second._compiled_code )
			{
				if ( iter->second._compiled_code->arg_size() != fn_args.size() ) 
					throw runtime_error( "function arity mismatch" );

				return make_pair( _builder.CreateCall( iter->second._compiled_code, fn_args, "calltmp" )
								, iter->second._name->_type );
			}
			else
			{
				return make_pair( iter->second._compiler_code( fn_args ), iter->second._name->_type );
			}
		}
		pair<Value*,type_ref_ptr> codegen_var( symbol& symbol )
		{
			variable_map::iterator iter = _fn_context.find( symbol._name );
			if ( iter == _fn_context.end() ) throw runtime_error( "failed to lookup variable" );
			return make_pair( _builder.CreateLoad( iter->second, symbol._name.c_str() )
				, &_type_system->get_type_ref( _str_table->register_str( "f32" ), type_ref_ptr_buffer() ) );
		}
		pair<Value*,type_ref_ptr> codegen_constant( constant& data )
		{
			switch( _type_system->to_base_numeric_type( *data._type ) )
			{
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name )		\
			case base_numeric_types::name:			\
				return make_pair( llvm_helper::llvm_constant_map<base_numeric_types::name>::parse( data._value )	\
								, data._type );
				CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
			}
			throw runtime_error( "failed to create numeric constant" );
		}

		pair<Value*,type_ref_ptr> codegen_expr( object_ptr expr )
		{
			if ( !expr ) throw runtime_error( "Invalid expression" );
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
				Value* last_statement_return = codegen_expr( progn_cell->_value ).first;
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

			FunctionType* fn_type = nullptr;
			vector<Type*> arg_types;
			vector<type_ref_ptr> arg_lisp_types;
			for_each( fn_args._data.begin(), fn_args._data.end(), [&]
			(object_ptr arg)
			{
				symbol& sym = object_traits::cast<symbol>(*arg);
				if ( sym._type == nullptr ) throw runtime_error( "failed to handle symbol with no type" );
				if ( sym._type->_name != _f32 ) throw runtime_error( "unrecogized data type" );
				arg_types.push_back( Type::getFloatTy(getGlobalContext()));
				arg_lisp_types.push_back( sym._type );
			} );

			type_ref& fn_lisp_type = _type_system->get_type_ref( _str_table->register_str( fn_def._name ), arg_lisp_types );
			
			function_def* fn_entry = nullptr;
			if ( fn_def._name.empty() == false )
			{
				pair<function_map::iterator, bool> inserter = _context.insert( make_pair( &fn_lisp_type, function_def() ) );
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
			type_system_ptr type_system = type_system::create_type_system( _alloc, _str_table );
			reader_ptr reader = reader::create_reader( factory, type_system, _str_table );
			object_ptr_buffer parse_result = reader->parse( data );
			//run through, code-gen the function defs and call the functions.
			InitializeNativeTarget();
			LLVMContext &Context = getGlobalContext();
			Module* module( new Module("my cool jit", Context) );
			_code_gen = make_shared<code_generator>( factory, type_system, _str_table, *module );

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


