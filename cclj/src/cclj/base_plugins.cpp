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

#include "cclj/llvm_base_numeric_type_helper.h"

using namespace cclj;
using namespace cclj::lisp;
using namespace cclj::plugins;
using namespace llvm;

namespace 
{

	template<typename data_type, typename allocator>
	data_buffer<data_type> allocate_buffer( allocator& alloc, data_buffer<data_type> buf )
	{
		if ( buf.size() == 0 ) return data_buffer<data_type>();
			 
		data_type* mem = reinterpret_cast<data_type*>( alloc.allocate( buf.size() * sizeof( data_type )
														, sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() ) );
		memcpy( mem, buf.begin(), buf.size() * sizeof( data_type ) );
		return data_buffer<data_type>( mem, buf.size() );
	}

	struct function_def_node;

	struct function_call_node : public ast_node
	{
		function_def_node&			_function;

		static const char* static_node_type() { return "function call"; }
		

		function_call_node(string_table_ptr str_table, const function_def_node& _fun );

		virtual bool executable_statement() const { return true; }

		pair<llvm_value_ptr, type_ref_ptr> compile_second_pass(compiler_context& context);
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


		ast_node& apply( reader_context& context, data_buffer<ast_node_ptr> args )
		{
			function_call_node* new_node = context._ast_allocator->construct<function_call_node>( context._string_table, *this );
			for_each( args.begin(), args.end(), [&]
			( ast_node_ptr arg )
			{
				new_node->children().push_back( *arg );
			} );
			return *new_node;
		}


		void compile_first_pass(compiler_context& context)
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

		pair<llvm_value_ptr, type_ref_ptr> compile_second_pass(compiler_context& context)
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



		static void initialize_function(compiler_context& context, Function& fn, data_buffer<symbol*> fn_args
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
	};

	
	function_call_node::function_call_node(string_table_ptr str_table, const function_def_node& _fun )
		: ast_node( str_table->register_str( static_node_type() ), *_fun._name._type )
		, _function( const_cast<function_def_node&>( _fun ) )
	{
	}

	
	pair<llvm_value_ptr, type_ref_ptr> function_call_node::compile_second_pass(compiler_context& context)
	{
		vector<llvm::Value*> fn_args;
		for ( auto iter = children().begin(), end = children().end(); iter != end; ++iter )
		{
			ast_node& node(*iter);
			fn_args.push_back( node.compile_second_pass( context ).first );
		}
		return make_pair( context._builder.CreateCall( _function._function, fn_args, "calltmp" )
						, _function._name._type );
	}


	class function_def_plugin : public compiler_plugin
	{
	public:
		typedef function_def_node ast_node_type;

		static const char* static_plugin_name() { return "function definition"; }

		function_def_plugin( string_table_ptr table )
			: compiler_plugin( table->register_str( static_plugin_name() ) )
		{
		}

		ast_node& type_check( reader_context& context, lisp::cons_cell& cell )
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
			function_def_node* new_node 
				= context._ast_allocator->construct<function_def_node>( context._string_table, fn_name, fn_type, args );
			ast_node_ptr last_body_eval( nullptr);
			for ( cons_cell* body_cell = &body; body_cell; body_cell = object_traits::cast<cons_cell>( body_cell->_next ) )
			{
				last_body_eval = &context._type_checker( *body_cell );
				new_node->children().push_back( *last_body_eval );
			}
			if ( &last_body_eval->type() != fn_name._type ) 
				throw runtime_error( "function return type does not equal last statement" );
			bool inserted = context._symbol_map->insert( make_pair( &fn_type, new_node ) ).second;
			if ( !inserted ) throw runtime_error( "duplicate symbol" );
			return *new_node;
		}
	};

	
	struct fake_ast_node : public ast_node
	{
		fake_ast_node( string_table_ptr st, type_library_ptr tl )
			: ast_node( st->register_str( "fake ast node" ), tl->get_type_ref( "fake ast node" ) )
		{
		}
		
		virtual pair<llvm_value_ptr, type_ref_ptr> compile_second_pass(compiler_context&)
		{
			return pair<llvm_value_ptr, type_ref_ptr>( nullptr, nullptr );
		}
	};
	//todo - create plugin system for the macro language.
	struct macro_preprocessor : public preprocessor
	{
		const symbol&			_name;
		data_buffer<object_ptr> _arguments;
		cons_cell&				_body;
		string_table_str		_quote;
		string_table_str		_unquote;
		
		macro_preprocessor( const symbol& name, data_buffer<object_ptr> args, const cons_cell& body, string_table_ptr st )
			: _name( name )
			, _arguments( args )
			, _body( const_cast<cons_cell&>( body ) )
			, _quote( st->register_str( "quote" ) )
			, _unquote( st->register_str( "unquote" ) )
		{
		}

		void quote_lisp_object( reader_context& context, object_ptr& item )
		{
			switch( item->type() )
			{
			case types::cons_cell:
				{
					cons_cell& arg_cell = object_traits::cast_ref<cons_cell>( item );
					symbol& fn_name = object_traits::cast_ref<symbol>( arg_cell._value );
					if ( fn_name._name == _unquote )
					{
						cons_cell& uq_arg = object_traits::cast_ref<cons_cell>( arg_cell._next );
						symbol& uq_arg_sym = object_traits::cast_ref<symbol>( uq_arg._value );
						auto sym_iter = context._preprocessor_symbols.find( uq_arg_sym._name );
						if ( sym_iter == context._preprocessor_symbols.end() )
							throw runtime_error( "failed to figure out preprocessor symbol" );
						item = sym_iter->second;
					}
					else
					{
						quote_list( context, arg_cell );
					}
				}
				break;
			case types::array:
				{
					array& arg_array = object_traits::cast_ref<array>( item );
					quote_array( context, arg_array );
				}
				break;
			}
		}

		void quote_list( reader_context& context, cons_cell& item )
		{
			for ( cons_cell* next_arg = object_traits::cast<cons_cell>( &item );
				next_arg; next_arg = object_traits::cast<cons_cell>( next_arg->_next ) )
			{
				quote_lisp_object( context, next_arg->_value );
			}
		}

		void quote_array( reader_context& context, array& item )
		{
			for ( size_t idx = 0, end = item._data.size(); idx < end; ++idx )
			{
				quote_lisp_object( context, item._data[idx] );
			}
		}

		object_ptr quote( reader_context& context, cons_cell& first_arg )
		{
			cons_cell& arg_val = object_traits::cast_ref<cons_cell>( first_arg._value );
			object_ptr retval = &arg_val;
			quote_list( context, arg_val );
			return retval;
		}

		object_ptr lisp_apply( reader_context& context, cons_cell& cell )
		{
			symbol& app_name = object_traits::cast_ref<symbol>( cell._value );
			if ( app_name._name == _quote )
				return quote( context, object_traits::cast_ref<cons_cell>( cell._next ) );
			else
				throw runtime_error( "failed to handle preprocessor symbol" );
		}
		
		virtual lisp::object_ptr preprocess( reader_context& context, cons_cell& callsite )
		{
			string_obj_ptr_map old_symbols( context._preprocessor_symbols );
			context._preprocessor_symbols.clear();
			preprocess_symbol_context preprocess( context._preprocessor_symbols );
			cons_cell* previous_arg( &callsite );
			for_each( _arguments.begin(), _arguments.end(), [&]
			( object_ptr arg )
			{
				cons_cell* next_arg = &object_traits::cast_ref<cons_cell>( previous_arg->_next );
				symbol& arg_name = object_traits::cast_ref<symbol>( arg );
				preprocess.add_symbol( arg_name._name, *next_arg->_value );
				previous_arg = next_arg;
			} );
			object_ptr retval = nullptr;
			for ( cons_cell* body_cell = &_body; body_cell
				; body_cell = object_traits::cast<cons_cell>( body_cell->_next ) )
			{
				retval = lisp_apply( context, object_traits::cast_ref<cons_cell>( body_cell->_value ) );
			}
			context._preprocessor_symbols = old_symbols;
			return retval;
		}
	};




	class macro_def_plugin : public compiler_plugin
	{
	public:
		const char* static_plugin_name() { return "macro definition"; }
		macro_def_plugin( string_table_ptr st )
			: compiler_plugin( st->register_str( static_plugin_name() ) )
		{
		}

		ast_node& type_check( reader_context& context, lisp::cons_cell& cell )
		{
			cons_cell& second_cell = object_traits::cast_ref<cons_cell>( cell._next );
			symbol& macro_name = object_traits::cast_ref<symbol>( second_cell._value );
			cons_cell& third_cell = object_traits::cast_ref<cons_cell>( second_cell._next );
			cons_cell& fourth_cell = object_traits::cast_ref<cons_cell>( third_cell._next );
			data_buffer<object_ptr> arg_array = object_traits::cast_ref<array>( third_cell._value )._data;
			macro_preprocessor* preprocess 
				= context._ast_allocator->construct<macro_preprocessor>( macro_name, arg_array, fourth_cell
																			, context._string_table );
			context._preprocess_objects[macro_name._name] = preprocess;
			return *context._ast_allocator->construct<fake_ast_node>( context._string_table, context._type_library );
		}

	};

	struct symbol_ast_node : public ast_node
	{
		symbol& _symbol;
		symbol_ast_node( string_table_ptr st, const symbol& s, const type_ref& t )
			: ast_node( st->register_str( "symbol node" ), t )
			, _symbol( const_cast<symbol&>( s ) )
		{
		}
		
		virtual pair<llvm_value_ptr, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			auto symbol_iter = context._variables.find( _symbol._name );
			if ( symbol_iter == context._variables.end() ) throw runtime_error( "missing symbol" );
			return make_pair( context._builder.CreateLoad( symbol_iter->second.first ), &type() );
		}
	};
	struct numeric_constant_ast_node : public ast_node
	{
		constant& _constant;
		numeric_constant_ast_node( string_table_ptr st, const constant& s )
			: ast_node( st->register_str( "numeric constant" ), *s._type )
			, _constant( const_cast<constant&>( s ) )
		{
		}
		virtual pair<llvm_value_ptr, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			switch( context._type_library->to_base_numeric_type( type() ) )
			{
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name )		\
			case base_numeric_types::name:			\
				return make_pair( llvm_helper::llvm_constant_map<base_numeric_types::name>::parse( _constant._value )	\
								, _constant._type );
				CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
			}
			throw runtime_error( "bad numeric constant" );
		}
	};

	struct if_ast_node : public ast_node
	{
		if_ast_node( string_table_ptr str_table, const type_ref& type )
			: ast_node( str_table->register_str( "if statement" ), type )
		{
		}

		virtual pair<llvm_value_ptr, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			ast_node& cond_node = children().front();
			ast_node& true_node = *cond_node.next_node();
			ast_node& false_node = *true_node.next_node();
			auto cond_result = cond_node.compile_second_pass( context );
			
			Function *theFunction = context._builder.GetInsertBlock()->getParent();
			
			BasicBlock *ThenBB = BasicBlock::Create(getGlobalContext(), "then", theFunction);

			BasicBlock *ElseBB = BasicBlock::Create(getGlobalContext(), "else");

			BasicBlock *MergeBB = BasicBlock::Create(getGlobalContext(), "ifcont");
			context._builder.CreateCondBr( cond_result.first, ThenBB, ElseBB );
			context._builder.SetInsertPoint( ThenBB );

			auto true_result = true_node.compile_second_pass( context );
			context._builder.CreateBr( MergeBB );

			//note that evaluating the node can change the builder's insert block (just as we are doing now).
			ThenBB = context._builder.GetInsertBlock();

			theFunction->getBasicBlockList().push_back( ElseBB );
			context._builder.SetInsertPoint( ElseBB );

			auto false_result = false_node.compile_second_pass( context );

			//should have been caught in the type check phase.
			if ( true_result.second != false_result.second ) 
				throw runtime_error( "Invalid if statement, expressions do not match type" );

			context._builder.CreateBr( MergeBB );

			//update else basic block
			ElseBB = context._builder.GetInsertBlock();

			
			// Emit merge block.
			theFunction->getBasicBlockList().push_back(MergeBB);
			context._builder.SetInsertPoint(MergeBB);
			PHINode *PN = context._builder.CreatePHI( context.type_ref_type(*true_result.second ), 2,
											"iftmp");
  
			PN->addIncoming(true_result.first, ThenBB);
			PN->addIncoming(false_result.first, ElseBB);
			return make_pair(PN, true_result.second);
		}
	};

	struct if_compiler_plugin : public compiler_plugin
	{
		if_compiler_plugin( string_table_ptr str_table )
			: compiler_plugin( str_table->register_str( "if compiler plugin" ) )
		{
		}
		
		virtual ast_node& type_check( reader_context& context, lisp::cons_cell& cell )
		{
			cons_cell& cond_cell = object_traits::cast_ref<cons_cell>( cell._next );
			cons_cell& true_cell = object_traits::cast_ref<cons_cell>( cond_cell._next );
			cons_cell& false_cell = object_traits::cast_ref<cons_cell>( true_cell._next );
			if ( false_cell._next ) throw runtime_error( "Invalid if state, must have only 3 sub lists" );
			ast_node& cond_node = context._type_checker( cond_cell );
			ast_node& true_node = context._type_checker( true_cell );
			ast_node& false_node = context._type_checker( false_cell );
			if ( context._type_library->to_base_numeric_type( cond_node.type() ) != base_numeric_types::i1 )
				throw runtime_error( "Invalid if condition type, must be boolean" );
			if ( &true_node.type() != &false_node.type() )
				throw runtime_error( "Invalid if statement, true and false branches have different types" );

			ast_node* retval = context._ast_allocator->construct<if_ast_node>( context._string_table, true_node.type() );
			retval->children().push_back( cond_node );
			retval->children().push_back( true_node );
			retval->children().push_back( false_node );
			return *retval;
		}
	};
	

	typedef function<llvm_value_ptr (IRBuilder<>& builder, Value* lhs, Value* rhs )> builder_binary_function;

	//this node is both registered as a global symbol and used at the callsite.
	struct binary_ast_node : public ast_node
	{
		builder_binary_function _function;
		binary_ast_node( string_table_str name, const type_ref& rettype, builder_binary_function build_fn )
			: ast_node( name, rettype )
			, _function( build_fn )
		{
		}

		
		virtual ast_node& apply( reader_context& context, data_buffer<ast_node_ptr> args )
		{
			binary_ast_node* retval 
				= context._ast_allocator->construct<binary_ast_node>( node_type(), type(), _function );
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
		type_ref* arg_array[2] = { &arg_type, &arg_type };
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


ast_node& base_language_plugins::type_check_apply( reader_context& context, lisp::cons_cell& cell )
{
	symbol& fn_name = object_traits::cast_ref<symbol>( cell._value );
	auto preprocess_iter = context._preprocess_objects.find( fn_name._name );
	if ( preprocess_iter != context._preprocess_objects.end() )
	{
		object_ptr replacement = preprocess_iter->second->preprocess( context, cell );
		cell._value = replacement;
		return context._type_checker( cell );
	}


	auto plugin_ptr_iter = context._special_forms->find( fn_name._name );
	if ( plugin_ptr_iter !=  context._special_forms->end() )
	{
		return plugin_ptr_iter->second->type_check( context, cell );
	}
	
	vector<type_ref_ptr> arg_types;
	vector<ast_node_ptr> resolved_args;
	//ensure we can find the function definition.
	for ( cons_cell* arg = object_traits::cast<cons_cell>( cell._next )
		; arg; arg = object_traits::cast<cons_cell>( arg->_next ) )
	{
		ast_node& eval_result = context._type_checker( *arg );
		arg_types.push_back( &eval_result.type() );
		resolved_args.push_back( &eval_result );
	}
	type_ref& fn_type = context._type_library->get_type_ref( fn_name._name, arg_types );
	auto context_node_iter = context._symbol_map->find( &fn_type );
	if ( context_node_iter == context._symbol_map->end() ) throw runtime_error( "unable to resolve function" );
	ast_node& new_node = context_node_iter->second->apply( context, resolved_args );
	return new_node;
}

ast_node& base_language_plugins::type_check_symbol( reader_context& context, lisp::cons_cell& cell )
{
	symbol& cell_symbol = object_traits::cast_ref<symbol>( cell._value );
	symbol_type_ref_map::iterator symbol_type = context._context_symbol_types.find( cell_symbol._name );
	if ( symbol_type == context._context_symbol_types.end() ) throw runtime_error( "unresolved symbol" );
	return *context._ast_allocator->construct<symbol_ast_node>( context._string_table, cell_symbol
																, *symbol_type->second );
}

ast_node& base_language_plugins::type_check_numeric_constant( reader_context& context, lisp::cons_cell& cell )
{
	constant& cell_constant = object_traits::cast_ref<constant>( cell._value );
	if ( context._type_library->to_base_numeric_type( *cell_constant._type ) 
		== base_numeric_types::no_known_type ) throw runtime_error( "invalid base numeric type" );
	return *context._ast_allocator->construct<numeric_constant_ast_node>( context._string_table, cell_constant );
}


void base_language_plugins::register_base_compiler_plugins( string_table_ptr str_table
											, string_plugin_map_ptr top_level_special_forms
											, string_plugin_map_ptr special_forms )
{
	
	top_level_special_forms->insert( make_pair( str_table->register_str( "defn" )
		, make_shared<function_def_plugin>( str_table ) ) );
	top_level_special_forms->insert( make_pair( str_table->register_str( "defmacro" )
		, make_shared<macro_def_plugin>( str_table ) ) );


	special_forms->insert( make_pair( str_table->register_str( "if" )
		, make_shared<if_compiler_plugin>( str_table ) ) );

}

void base_language_plugins::initialize_function(compiler_context& context, Function& fn, data_buffer<symbol*> fn_args
														, variable_context& var_context)
{
	function_def_node::initialize_function( context, fn, fn_args, var_context );
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

	//boolean operations


	register_binary_float_fn( fn, str_table->register_str( "float lt" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
							{
								return builder.CreateFCmpULT( lhs, rhs, "tmpcmp" );
							}, str_table->register_str( "<" )
								, ast_allocator, false );

							
	register_binary_float_fn( fn, str_table->register_str( "float gt" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
							{
								return builder.CreateFCmpUGT( lhs, rhs, "tmpcmp" );
							}, str_table->register_str( ">" )
								, ast_allocator, false );

	register_binary_float_fn( fn, str_table->register_str( "float eq" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
							{
								return builder.CreateFCmpUEQ( lhs, rhs, "tmpcmp" );
							}, str_table->register_str( "==" )
								, ast_allocator, false );
							
	register_binary_float_fn( fn, str_table->register_str( "float neq" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
							{
								return builder.CreateFCmpUNE( lhs, rhs, "tmpcmp" );
							}, str_table->register_str( "!=" )
								, ast_allocator, false );
							
	register_binary_float_fn( fn, str_table->register_str( "float ge" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
							{
								return builder.CreateFCmpUGE( lhs, rhs, "tmpcmp" );
							}, str_table->register_str( ">=" )
								, ast_allocator, false );
							
	register_binary_float_fn( fn, str_table->register_str( "float le" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
							{
								return builder.CreateFCmpULE( lhs, rhs, "tmpcmp" );
							}, str_table->register_str( ">=" )
								, ast_allocator, false );

							
	register_binary_integer_fn( fn, str_table->register_str( "int eq" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateICmpEQ( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "==" )
								, ast_allocator, false );
								
	register_binary_integer_fn( fn, str_table->register_str( "int neq" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateICmpNE( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "!=" )
								, ast_allocator, false );
	
	register_binary_unsigned_integer_fn( fn, str_table->register_str( "uint lt" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateICmpULT( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "<" )
								, ast_allocator, false );

	register_binary_unsigned_integer_fn( fn, str_table->register_str( "uint le" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateICmpULE( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "<=" )
								, ast_allocator, false );

	register_binary_unsigned_integer_fn( fn, str_table->register_str( "uint gt" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateICmpUGT( lhs, rhs, "tmpadd" );
								}, str_table->register_str( ">" )
								, ast_allocator, false );
								
	register_binary_unsigned_integer_fn( fn, str_table->register_str( "uint ge" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateICmpUGE( lhs, rhs, "tmpadd" );
								}, str_table->register_str( ">=" )
								, ast_allocator, false );

								


	register_binary_signed_integer_fn( fn, str_table->register_str( "sint lt" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateICmpSLT( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "<" )
								, ast_allocator, false );

	register_binary_signed_integer_fn( fn, str_table->register_str( "sint le" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateICmpSLE( lhs, rhs, "tmpadd" );
								}, str_table->register_str( "<=" )
								, ast_allocator, false );

	register_binary_signed_integer_fn( fn, str_table->register_str( "sint gt" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateICmpSGT( lhs, rhs, "tmpadd" );
								}, str_table->register_str( ">" )
								, ast_allocator, false );
								
	register_binary_signed_integer_fn( fn, str_table->register_str( "sint ge" ), type_lib
								, []( IRBuilder<>& builder, llvm_value_ptr lhs, llvm_value_ptr rhs )
								{
									return builder.CreateICmpSGE( lhs, rhs, "tmpadd" );
								}, str_table->register_str( ">=" )
								, ast_allocator, false );
}


