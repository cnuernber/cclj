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
		//note that the _name._type signifies the return type of the function
		//not the argument types.  These are not represented here.
		symbol*				_name;
		Function*			_compiled_code;
		compiler_intrinsic	_compiler_code;
		

		function_def() : _name( nullptr ), _compiled_code( nullptr ) {}
	};

	struct pod_type
	{
		vector<symbol*> _fields;
		StructType*		_llvm_type;
		pod_type() : _llvm_type( nullptr ) {}
		pair<unsigned,type_ref_ptr> find_ref( string_table_str arg )
		{
			vector<symbol*>::iterator iter = find_if( _fields.begin(), _fields.end(), [=](symbol* sym)
			{
				return arg == sym->_name;
			} );
			if ( iter != _fields.end() )
			{
				unsigned idx = static_cast<unsigned>( iter - _fields.begin() );
				return make_pair( idx, (*iter)->_type );
			}
			throw runtime_error( "unable to find symbol" );
		}
	};

	typedef function<pair<Value*, type_ref_ptr> (cons_cell& cell)> compiler_special_form;
	typedef function<type_ref_ptr(cons_cell& cell)> top_level_compiler_special_form;

	typedef unordered_map<type_ref_ptr, function_def> function_map;
	typedef unordered_map<string_table_str, pair<AllocaInst*,type_ref_ptr> > variable_map;
	typedef unordered_map<string_table_str, Function*> compiler_map;
	typedef unordered_map<string_table_str, compiler_special_form> compiler_special_form_map;
	typedef unordered_map<string_table_str, top_level_compiler_special_form> top_level_compiler_special_form_map;
	typedef unordered_map<type_ref_ptr, Type*> type_llvm_type_map;
	typedef unordered_map<type_ref_ptr,pod_type> type_pod_map;

	struct code_generator;
	typedef object_ptr (*compiler_function)( code_generator*, object_ptr );


	struct code_generator : public noncopyable
	{
		factory_ptr							_factory;
		type_system_ptr						_type_system;
		string_table_ptr					_str_table;
		IRBuilder<>							_builder;
		Module&								_module;
		shared_ptr<ExecutionEngine>			_exec_engine;
		shared_ptr<FunctionPassManager>		_fpm;
		function_map						_context;
		variable_map						_fn_context;
		compiler_special_form_map			_special_forms;
		top_level_compiler_special_form_map	_top_level_special_forms;
		type_llvm_type_map					_type_map;
		type_pod_map						_pod_types;

		
		code_generator( factory_ptr f, type_system_ptr t, string_table_ptr str_table, Module& m )
			: _factory( f )
			, _type_system( t )
			, _str_table( str_table )
			, _builder( getGlobalContext() )
			, _module( m )
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
			register_top_level_special_form( bind( &code_generator::defn_special_form, this, std::placeholders::_1 ), "defn" );
			register_top_level_special_form( bind( &code_generator::defpod_special_form, this, std::placeholders::_1 ), "defpod" );
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
			
			type_llvm_type_map::iterator iter = _type_map.find( &type );
			if ( iter != _type_map.end() ) return iter->second;
			base_numeric_types::_enum val = _type_system->to_base_numeric_type( type );
			Type* base_type = nullptr;
			switch( val )
			{
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name )				\
			case base_numeric_types::name: base_type		\
				= llvm_helper::llvm_constant_map<base_numeric_types::name>::type(); break;
				CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
			default:
				throw runtime_error( "unable to find type" );
			}
			//Just in case someone adds another lookup system.
			if ( base_type == nullptr )
				throw runtime_error( "unable to find type" );

			_type_map.insert( make_pair( &type, base_type ) );
			return base_type;
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
			typedef pair<string_table_str, pair<AllocaInst*,type_ref_ptr> > var_entry; 
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
				_fn_context[name._name] = make_pair(data, expr_result.second);
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

		void register_top_level_special_form( top_level_compiler_special_form fn, const char* name )
		{
			_top_level_special_forms.insert( make_pair( _str_table->register_str( name ), fn ) );
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
		vector<string> split_symbol( symbol& sym )
		{
			vector<string> retval;
			string temp(sym._name.c_str());
			size_t last_offset = 0;
			for ( size_t off = temp.find( '.' ); off != string::npos;
				off = temp.find( '.', off+1 ) )
			{
				retval.push_back( temp.substr( last_offset, off - last_offset ) );
				last_offset = off + 1;
			}
			if ( last_offset < temp.size() )
			{
				retval.push_back( temp.substr( last_offset, temp.size() - last_offset ) );
			}
			return retval;
		}
		pair<Value*,type_ref_ptr> codegen_var( symbol& symbol )
		{
			vector<string> splitter( split_symbol( symbol ) );
			if ( splitter.empty() ) throw runtime_error( "invalid symbol" );
			variable_map::iterator iter = _fn_context.find( _str_table->register_str( splitter[0].c_str() ) );
			if ( iter == _fn_context.end() ) throw runtime_error( "failed to lookup variable" );
			Value* load_var = iter->second.first;
			type_ref_ptr stack_type = iter->second.second;
			if ( splitter.size() > 1 )
			{
				vector<Value*> GEPargs;
				GEPargs.push_back( ConstantInt::get( Type::getInt32Ty(getGlobalContext()), 0 ) );
				for ( size_t idx = 1, end = splitter.size(); idx < end; ++idx )
				{
					type_pod_map::iterator pod_type_iter = _pod_types.find( stack_type );
					if( pod_type_iter == _pod_types.end() ) throw runtime_error( "symbol does not point to pod type" );
					pod_type* pod = &pod_type_iter->second;
					pair<unsigned,type_ref_ptr> sym_index = pod->find_ref( _str_table->register_str( splitter[idx].c_str() ) );
					GEPargs.push_back( ConstantInt::get( Type::getInt32Ty(getGlobalContext()), sym_index.first ) ); 
					stack_type = sym_index.second;
				}
				load_var = _builder.CreateInBoundsGEP(load_var, GEPargs);
			}
			Value* loaded_item = _builder.CreateLoad( load_var );
			return make_pair( loaded_item, stack_type );
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

		type_ref_ptr codegen_function_body( Function* fn, cons_cell& fn_body )
		{
			//codegen the body of the function
			pair<Value*,type_ref_ptr> last_statement_return( nullptr, nullptr );
			for ( cons_cell* progn_cell = &fn_body; progn_cell
						; progn_cell = object_traits::cast<cons_cell>( progn_cell->_next ) )
			{
				last_statement_return = codegen_expr( progn_cell->_value );
			}
			if ( last_statement_return.first )
			{
				_builder.CreateRet( last_statement_return.first );
				verifyFunction(*fn );
				_fpm->run( *fn );
			}
			else
				throw runtime_error( "function definition failed" );
			return last_statement_return.second;
		}

		void initialize_function(Function& fn, data_buffer<symbol*> fn_args)
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
			_builder.SetInsertPoint(function_entry_block);
			Function::arg_iterator AI = fn.arg_begin();
			IRBuilder<> entry_block_builder( &fn.getEntryBlock(), fn.getEntryBlock().begin() );
			for (unsigned Idx = 0, e = fn_args.size(); Idx != e; ++Idx, ++AI) {
				symbol& arg_def( *fn_args[Idx] );
				// Create an alloca for this variable.
				if ( arg_def._type == nullptr ) throw runtime_error( "Invalid function argument" );
				AllocaInst *Alloca = entry_block_builder.CreateAlloca(type_ref_type( *arg_def._type ), 0,
						arg_def._name.c_str());

				// Store the initial value into the alloca.
				_builder.CreateStore(AI, Alloca);
				
				bool inserted = _fn_context.insert(make_pair( arg_def._name, make_pair(Alloca, arg_def._type ) ) ).second;
				if ( !inserted )
					throw runtime_error( "duplicate function argument name" );
			}
		}
		
		pair<Function*,type_ref_ptr> codegen_function_def( cons_cell& defn_cell )
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
			vector<symbol*> fn_arg_symbols;
			for_each( fn_args._data.begin(), fn_args._data.end(), [&]
			(object_ptr arg)
			{
				symbol& sym = object_traits::cast<symbol>(*arg);
				if ( sym._type == nullptr ) throw runtime_error( "failed to handle symbol with no type" );
				fn_arg_symbols.push_back( &sym );
				arg_types.push_back( type_ref_type( *sym._type ) );
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
			}

			if ( fn_def._type == nullptr ) throw runtime_error( "unrecoginzed function return type" );

			Type* rettype = type_ref_type( *fn_def._type );
			fn_type = FunctionType::get(rettype, arg_types, false);
			Function* fn = Function::Create( fn_type, Function::ExternalLinkage, fn_def._name.c_str(), &_module );
			_type_map.insert( make_pair( &fn_lisp_type, fn_type ) );
			
			if ( fn_entry )
				fn_entry->_compiled_code = fn;

			initialize_function( *fn, fn_arg_symbols );

			codegen_function_body( fn, fn_body );

			_fn_context.clear();
			return make_pair(fn, fn_def._type);
		}
		type_ref_ptr defn_special_form( cons_cell& defn_cell )
		{
			return codegen_function_def(defn_cell).second;
		}

		type_ref_ptr defpod_special_form( cons_cell& defpod_cell )
		{
			//Create the struct, and create a function that given all the fields of the struct can
			//return the struct.
			cons_cell& name_cell = object_traits::cast_ref<cons_cell>( defpod_cell._next  );
			symbol& name = object_traits::cast_ref<symbol>( name_cell._value );
			type_ref& this_pod_type = _type_system->get_type_ref( name._name );
			pair<type_pod_map::iterator,bool> insert_result = _pod_types.insert(make_pair( &this_pod_type, pod_type() ));
			if ( insert_result.second == false ) throw runtime_error( "redefinition of pod types is not allowed" );
			pod_type& new_pod = insert_result.first->second;
			vector<symbol*>& fields(new_pod._fields);
			vector<type_ref*> fn_arg_types;
			vector<Type*> types;
			for ( cons_cell* field_cell = object_traits::cast<cons_cell>( name_cell._next );
				field_cell; field_cell = object_traits::cast<cons_cell>( field_cell->_next ) )
			{
				symbol& field_name = object_traits::cast_ref<symbol>( field_cell->_value );
				if ( field_name._type == nullptr ) throw runtime_error( "invalid pod type field" );
				fields.push_back( &field_name );
				types.push_back( type_ref_type( *field_name._type ) );
				fn_arg_types.push_back( field_name._type );
			}
			new_pod._llvm_type = StructType::create( getGlobalContext(), types );
			_type_map.insert( make_pair( &this_pod_type, new_pod._llvm_type ) );
			type_ref& new_fn_type = _type_system->get_type_ref( name._name, fn_arg_types );
			FunctionType* fn_llvm_type = FunctionType::get( new_pod._llvm_type, types, false );
			_type_map.insert( make_pair( &new_fn_type, fn_llvm_type ) );
			Function* constructor = Function::Create( fn_llvm_type, Function::ExternalLinkage, name._name.c_str(), &_module );
			//output the function
			initialize_function( *constructor, new_pod._fields );
			IRBuilder<> entry_block_builder( &constructor->getEntryBlock(), constructor->getEntryBlock().begin() );
			AllocaInst* struct_alloca = entry_block_builder.CreateAlloca( new_pod._llvm_type, 0, "retval" );
			int idx = 0;
			for_each( fields.begin(), fields.end(), [&](symbol* field)
			{
				Value* val_ptr = _builder.CreateConstInBoundsGEP2_32( struct_alloca, 0, idx, field->_name.c_str() );
				Value* arg_val = _builder.CreateLoad( _fn_context.find( field->_name )->second.first );
				_builder.CreateStore( arg_val, val_ptr );
				++idx;
			} );
			Value* retval = _builder.CreateLoad( struct_alloca );
			_builder.CreateRet( retval );
			verifyFunction(*constructor );
			_fpm->run( *constructor );
			_fn_context.clear();
			function_def new_def;
			symbol* fn_symbol = _factory->create_symbol();
			fn_symbol->_name = name._name;
			fn_symbol->_type = &this_pod_type;
			new_def._name = fn_symbol;
			new_def._compiled_code = constructor;
			//wowsa this is pretty tough.
			_context.insert( make_pair( &new_fn_type, new_def ) );
			return &this_pod_type;
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
					symbol& item_name = object_traits::cast_ref<symbol>(cell->_value);
					top_level_compiler_special_form_map::iterator sp 
						= _code_gen->_top_level_special_forms.find( item_name._name );
					if ( sp != _code_gen->_top_level_special_forms.end() )
					{
						sp->second( *cell );
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
						type_ref_ptr ref = _code_gen->codegen_function_body( fn, *progn_cell );
						if ( ref != &type_system->get_type_ref( base_numeric_types::f32 ) )
							throw runtime_error( "Expression is not of float type" );
							
						void *fn_ptr= _code_gen->_exec_engine->getPointerToFunction(fn);
						if ( !fn_ptr ) throw runtime_error( "function definition failed" );
						anon_fn_type anon_fn = reinterpret_cast<anon_fn_type>( fn_ptr );
						retval = anon_fn();
					}
				}
			}

			return retval;
		}
	};
}

state_ptr state::create_state() { return make_shared<state_impl>(); }


