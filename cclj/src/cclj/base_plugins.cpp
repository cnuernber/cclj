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

	symbol_resolution_context_ptr resolve_symbol( reader_context& context, lisp::symbol& sym )
	{
		vector<string> parts = split_symbol( sym );
		string_table_str initial = context._string_table->register_str( parts[0] );
		symbol_type_ref_map::iterator symbol_type = context._context_symbol_types.find( initial );
		if ( symbol_type == context._context_symbol_types.end() ) throw runtime_error( "unresolved symbol" );
		
		symbol_resolution_context_ptr res_context = symbol_resolution_context::create( initial, *symbol_type->second );
		if ( parts.size() > 1 )
		{
			for ( size_t idx = 1, end = parts.size(); idx < end; ++idx )
			{
				auto next_type_iter = context._symbol_map->find( symbol_type->second );
				if ( next_type_iter == context._symbol_map->end() )
					throw runtime_error( "unresolved symbol" );

				//magic of the gep, have to add zero to start it off.
				if ( idx == 1 )
				{
					res_context->add_GEP_index( 0, *next_type_iter->first );
				}
				auto next_part = context._string_table->register_str( parts[idx] );
				next_type_iter->second->resolve_symbol( context, next_part, *res_context );
			}
		}
		return res_context;
	}

	ast_node& type_check_function_application( reader_context& context, lisp::cons_cell& cell )
	{
		symbol& fn_name = object_traits::cast_ref<symbol>( cell._value );
		vector<type_ref_ptr> arg_types;
		vector<ast_node_ptr> resolved_args;
		//ensure we can find the function definition.
		for ( cons_cell* arg = object_traits::cast<cons_cell>( cell._next )
			; arg; arg = object_traits::cast<cons_cell>( arg->_next ) )
		{
			ast_node& eval_result = context._type_checker( arg->_value );
			arg_types.push_back( &eval_result.type() );
			resolved_args.push_back( &eval_result );
		}
		type_ref& fn_type = context._type_library->get_type_ref( fn_name._name, arg_types );
		auto context_node_iter = context._symbol_map->find( &fn_type );
		if ( context_node_iter == context._symbol_map->end() ) throw runtime_error( "unable to resolve function" );
		ast_node& new_node = context_node_iter->second->apply( context, resolved_args );
		return new_node;
	}


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

	struct llvm_function_provider
	{
	protected:
		virtual ~llvm_function_provider(){}
	public:
		virtual type_ref& return_type() = 0;
		virtual Function& function() = 0; 
	};

	struct function_call_node : public ast_node
	{
		llvm_function_provider&			_function;

		static const char* static_node_type() { return "function call"; }
		

		function_call_node(string_table_ptr str_table, const llvm_function_provider& _fun )
		: ast_node( str_table->register_str( static_node_type() )
					, const_cast<llvm_function_provider&>( _fun ).return_type()  )
		, _function( const_cast<llvm_function_provider&>( _fun ) )
		{
		}

		virtual bool executable_statement() const { return true; }

		pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			vector<llvm::Value*> fn_args;
			for ( auto iter = children().begin(), end = children().end(); iter != end; ++iter )
			{
				ast_node& node(*iter);
				auto pass_result = node.compile_second_pass( context ).first;
				if ( pass_result )
					fn_args.push_back( pass_result.get() );
			}
			
			type_ref& rettype = _function.return_type();
			const char* twine = "calltmp";
			bool is_void = &rettype == &context._type_library->get_void_type();
			if ( is_void )
				twine = "";
			Value* retval = context._builder.CreateCall( &_function.function(), fn_args, twine );
			if ( is_void )
				retval = nullptr;
			return make_pair( retval
							, &rettype );
		}
	};

	struct function_def_node : public ast_node, public llvm_function_provider
	{
		static const char* static_node_type() { return "function definition"; }

		lisp::symbol&				_name;
		type_ref&					_my_type;
		data_buffer<lisp::symbol*>	_arguments;
		llvm_function_ptr			_function;

		function_def_node( string_table_ptr str_table, const lisp::symbol& name
							, const type_ref& mt, data_buffer<lisp::symbol*> arguments )
			: ast_node( str_table->register_str( static_node_type() ), *name._evaled_type )
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
			vector<type_ref_ptr> cclj_arg_types;
			for_each( _arguments.begin(), _arguments.end(), [&]
			(symbol* sym )
			{
				if ( sym->_evaled_type != &context._type_library->get_void_type() )
					arg_types.push_back( context.type_ref_type( *sym->_evaled_type ).get() );
				cclj_arg_types.push_back( sym->_evaled_type );
			} );
			type_ref& cclj_fn_type = context._type_library->get_type_ref( _name._name, cclj_arg_types );
			llvm_type_ptr rettype = context.type_ref_type( *_name._evaled_type ).get();
			FunctionType* fn_type = FunctionType::get(rettype, arg_types, false);
			string name_mangle( cclj_fn_type.to_string() );
			
			_function = Function::Create( fn_type, Function::ExternalLinkage, name_mangle.c_str(), &context._module );
		}

		pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			variable_context fn_context( context._variables );
			initialize_function( context, *_function, _arguments, fn_context );
			pair<llvm_value_ptr_opt, type_ref_ptr> last_statement( nullptr, nullptr );
			for ( auto iter = children().begin(), end = children().end(); iter != end; ++iter )
			{
				last_statement = iter->compile_second_pass( context );
			}
			Value* retval = nullptr;
			if ( last_statement.first.valid() )
				retval = context._builder.CreateRet( last_statement.first.get() );
			else
				context._builder.CreateRetVoid();
			verifyFunction(*_function );
			context._fpm.run( *_function );
			return make_pair( retval, _name._evaled_type );
		}

		
		virtual type_ref& return_type() 
		{
			if ( _name._evaled_type == nullptr ) throw runtime_error( "Invalid function return type" );
			return *_name._evaled_type; 
		}

		virtual Function& function() 
		{ 
			if ( _function == nullptr )
				throw runtime_error( "Function definition first pass not called yet" );
			return *_function;
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
				if ( arg_def._evaled_type == nullptr ) throw runtime_error( "Invalid function argument" );
				AllocaInst *Alloca = nullptr;
				if ( arg_def._evaled_type != &context._type_library->get_void_type() ) 
				{
					Alloca = entry_block_builder.CreateAlloca(context.type_ref_type( *arg_def._evaled_type ).get()
																, 0, arg_def._name.c_str());

					// Store the initial value into the alloca.
					context._builder.CreateStore(AI, Alloca);
				}
				var_context.add_variable( arg_def._name, Alloca, *arg_def._evaled_type );
			}
		}
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

		ast_node& type_check( reader_context& context, lisp::cons_cell& cell )
		{
			cons_cell& fn_name_cell = object_traits::cast_ref<cons_cell>( cell._next );
			symbol& fn_name = object_traits::cast_ref<symbol>( fn_name_cell._value );
			context.symbol_type( fn_name );
			cons_cell& arg_array_cell = object_traits::cast_ref<cons_cell>( fn_name_cell._next );
			array&  arg_array = object_traits::cast_ref<array>( arg_array_cell._value );
			cons_cell& body = object_traits::cast_ref<cons_cell>( arg_array_cell._next );
			symbol_type_context symbol_context( context._context_symbol_types );
			vector<type_ref_ptr> type_array;
			vector<symbol*> symbol_array;
			for ( size_t idx = 0, end = arg_array._data.size(); idx < end; ++idx )
			{
				symbol& arg_symbol = object_traits::cast_ref<symbol>( arg_array._data[idx] );
				type_ref& arg_type = context.symbol_type( arg_symbol );
				type_array.push_back( &arg_type );
				symbol_array.push_back( &arg_symbol );
				symbol_context.add_symbol( arg_symbol._name, arg_type );
			}
			type_ref& fn_type = context._type_library->get_type_ref( fn_name._name, type_array );
			data_buffer<symbol*> args = allocate_buffer<symbol*>( *context._ast_allocator, symbol_array );
			function_def_node* new_node 
				= context._ast_allocator->construct<function_def_node>( context._string_table, fn_name, fn_type, args );
			ast_node_ptr last_body_eval( nullptr);
			for ( cons_cell* body_cell = &body; body_cell; body_cell = object_traits::cast<cons_cell>( body_cell->_next ) )
			{
				last_body_eval = &context._type_checker( body_cell->_value );
				new_node->children().push_back( *last_body_eval );
			}
			if ( &last_body_eval->type() != fn_name._evaled_type ) 
				throw runtime_error( "function return type does not equal last statement" );
			auto inserter = context._symbol_map->insert( make_pair( &fn_type, new_node ) );
			if ( inserter.second == false  ) 
			{
				if ( inserter.first->second )
					throw runtime_error( "duplicate symbol" );
				inserter.first->second = new_node;
			}
			return *new_node;
		}
	};

	
	struct fake_ast_node : public ast_node
	{
		fake_ast_node( string_table_ptr st, type_library_ptr tl )
			: ast_node( st->register_str( "fake ast node" ), tl->get_type_ref( "fake ast node" ) )
		{
		}
		
		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context&)
		{
			return pair<llvm_value_ptr_opt, type_ref_ptr>( nullptr, nullptr );
		}
	};
	//todo - create plugin system for the macro language.
	struct macro_preprocessor : public compiler_plugin
	{
		const symbol&			_name;
		data_buffer<object_ptr> _arguments;
		cons_cell&				_body;

		static const char* static_type() { return "macro_preprocessor"; }
		
		macro_preprocessor( const symbol& name, data_buffer<object_ptr> args, const cons_cell& body, string_table_ptr st )
			: compiler_plugin( st->register_str( static_type() ) )
			, _name( name )
			, _arguments( args )
			, _body( const_cast<cons_cell&>( body ) )
		{
		}

		static void quote_lisp_object( reader_context& context, object_ptr& item )
		{
			switch( item->type() )
			{
			case types::cons_cell:
				{
					cons_cell& arg_cell = object_traits::cast_ref<cons_cell>( item );
					symbol& fn_name = object_traits::cast_ref<symbol>( arg_cell._value );
					if ( fn_name._name == context._string_table->register_str( "unquote" ) )
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
			case types::symbol:
				{
					symbol& sym = object_traits::cast_ref<symbol>( item );
					if ( sym._unevaled_type )
						quote_list( context, *sym._unevaled_type );
				}
				break;
			case types::constant:
				{
					constant& cons = object_traits::cast_ref<constant>( item );
					if ( cons._unevaled_type )
						quote_list( context, *cons._unevaled_type );
				}
				break;
			default: break; //leave item as is
			}
		}

		static void quote_list( reader_context& context, cons_cell& item )
		{
			for ( cons_cell* next_arg = object_traits::cast<cons_cell>( &item );
				next_arg; next_arg = object_traits::cast<cons_cell>( next_arg->_next ) )
			{
				quote_lisp_object( context, next_arg->_value );
			}
		}

		static void quote_array( reader_context& context, array& item )
		{
			for ( size_t idx = 0, end = item._data.size(); idx < end; ++idx )
			{
				quote_lisp_object( context, item._data[idx] );
			}
		}

		static object_ptr quote( reader_context& context, cons_cell& first_arg )
		{
			cons_cell& arg_val = object_traits::cast_ref<cons_cell>( first_arg._value );
			object_ptr retval = &arg_val;
			quote_list( context, arg_val );
			return retval;
		}

		static object_ptr lisp_apply( reader_context& context, cons_cell& cell )
		{
			symbol& app_name = object_traits::cast_ref<symbol>( cell._value );
			if ( app_name._name == context._string_table->register_str( "quote" ) )
				return quote( context, object_traits::cast_ref<cons_cell>( cell._next ) );
			else
			{
				auto iter = context._preprocessor_evaluators.find( app_name._name );
				if ( iter != context._preprocessor_evaluators.end() )
				{
					return iter->second->eval( context, cell );
				}
				throw runtime_error( "unable to eval lisp preprocessor symbol" );
			}
		}

		static double eval_constant( reader_context& /*context*/, constant& src )
		{
			return std::stod( src._unparsed_number.c_str() );
		}

		static object_ptr double_to_constant( reader_context& context, double& val )
		{
			constant* retval = context._factory->create_constant();
			cons_cell* typeval = context._factory->create_cell();
			symbol* type_name = context._factory->create_symbol();
			char data_buf[1024];
			sprintf( data_buf, "%f", val );
			retval->_unparsed_number = context._string_table->register_str( data_buf );
			retval->_unevaled_type = typeval;
			typeval->_value = type_name;
			type_name->_name = context._string_table->register_str( "f64" );
			return retval;
		}

		static object_ptr eval_symbol( reader_context& context, symbol& src )
		{
			auto iter = context._preprocessor_symbols.find( src._name );
			if ( iter == context._preprocessor_symbols.end() )
				throw runtime_error( "Error evaulating symbol" );
			return iter->second;
		}

		static object_ptr lisp_eval( reader_context& context, object_ptr src )
		{
			if ( src == nullptr )
				throw runtime_error( "invalid lisp evaluation" );
			switch( src->type() )
			{
			case types::array: throw runtime_error( "unable to eval an array" );
			case types::symbol: return eval_symbol( context, object_traits::cast_ref<symbol>( src ) );
			case types::constant: return src;
			case types::cons_cell: return lisp_apply( context, object_traits::cast_ref<cons_cell>( src ) );
			default: break;
			}
			throw runtime_error( "unable to evaluate lisp symbol" );
		}

		static vector<object_ptr> eval_fn_arguments( reader_context& context, cons_cell& callsite )
		{
			vector<object_ptr> arg_list;
			for ( cons_cell* arg_cell = object_traits::cast<cons_cell>( callsite._next ); arg_cell
				; arg_cell = object_traits::cast<cons_cell>( arg_cell->_next ) )
				arg_list.push_back( lisp_eval( context, arg_cell->_value ) );
			return arg_list;
		}
		
		virtual ast_node& type_check( reader_context& context, cons_cell& callsite )
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
			return context._type_checker( retval );
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
			compiler_plugin_ptr preprocess 
				= make_shared<macro_preprocessor>( macro_name, arg_array, fourth_cell
																			, context._string_table );
			(*context._special_forms)[macro_name._name] = preprocess;
			return *context._ast_allocator->construct<fake_ast_node>( context._string_table, context._type_library );
		}

	};

	typedef function<object_ptr(reader_context&, cons_cell&)> generic_lisp_special_form_function;
	struct generic_lisp_special_form : public lisp_evaluator
	{
		generic_lisp_special_form_function _fn;
		generic_lisp_special_form( generic_lisp_special_form_function fn ) : _fn( fn ) {}
		
		virtual lisp::object_ptr eval( reader_context& context, cons_cell& cell )
		{
			return _fn( context, cell );
		}
		static object_ptr let_eval( reader_context& context, cons_cell& cell )
		{
			cons_cell& assign_array = object_traits::cast_ref<cons_cell>( cell._next );
			object_ptr_buffer assign_data = object_traits::cast_ref<array>( assign_array._value )._data;
			cons_cell& body_start = object_traits::cast_ref<cons_cell>( assign_array._next );
			preprocess_symbol_context preprocess( context._preprocessor_symbols );
			for( size_t idx = 0, end = assign_data.size(); idx < end; idx += 2 )
			{
				symbol& var_name = object_traits::cast_ref<symbol>( assign_data[idx] );
				object_ptr expr = macro_preprocessor::lisp_eval( context, assign_data[idx+1] );
				preprocess.add_symbol( var_name._name, *expr );
			}
			object_ptr retval( nullptr );
			for ( cons_cell* body = &body_start; body; body = object_traits::cast<cons_cell>( body->_next ) )
				retval = macro_preprocessor::lisp_eval( context, body->_value );
			if ( retval == nullptr )
				throw runtime_error( "Let return value is null" );
			return retval;
		}
		static object_ptr if_eval( reader_context& context, cons_cell& cell )
		{
			//what evaluates to true?
			cons_cell& compare_cell = object_traits::cast_ref<cons_cell>( cell._next );
			cons_cell& true_cell  = object_traits::cast_ref<cons_cell>( compare_cell._next );
			cons_cell& false_cell = object_traits::cast_ref<cons_cell>( true_cell._next );
			object_ptr result = macro_preprocessor::lisp_eval( context, compare_cell._value );
			bool test_value = false;
			if ( result )
			{
				if ( result->type() == types::constant )
					test_value = macro_preprocessor::eval_constant( context, object_traits::cast_ref<constant>( result ) ) != 0.0;
				else if ( result->type() == types::cons_cell )
				{
					cons_cell& cell = object_traits::cast_ref<cons_cell>( result );
					if ( cell._next || cell._value )
						test_value = true;
				}
			}
			if ( test_value )
				return macro_preprocessor::lisp_eval( context, true_cell._value );
			else
				return macro_preprocessor::lisp_eval( context, false_cell._value );
		}

		static void register_functions( string_table_ptr st, string_lisp_evaluator_map& map )
		{
			map.insert( make_pair( st->register_str( "let" ), make_shared<generic_lisp_special_form> (
					[]( reader_context& ctx, cons_cell& cell ) { return let_eval( ctx, cell ); } ) ) );
			map.insert( make_pair( st->register_str( "if" ), make_shared<generic_lisp_special_form> (
					[]( reader_context& ctx, cons_cell& cell ) { return if_eval( ctx, cell ); } ) ) );
		}
	};

	//a generic function is just passed the body.  The arguments have already been evaluated
	//and added to the symbol table.
	typedef function<object_ptr(reader_context&, vector<object_ptr>&)> generic_lisp_function;
	struct generic_preprocessor_function : public lisp_evaluator
	{
		generic_lisp_function _function;
		generic_preprocessor_function( generic_lisp_function fn )
			: _function( fn )
		{
		}
		virtual lisp::object_ptr eval( reader_context& context, cons_cell& cell )
		{
			vector<object_ptr> arg_vals = macro_preprocessor::eval_fn_arguments( context, cell );
			return _function( context, arg_vals );
		}

		static object_ptr plus( reader_context& context, vector<object_ptr>& args )
		{
			double total = 0;
			for_each( args.begin(), args.end(), [&] 
			(object_ptr arg )
			{
				constant& cons = object_traits::cast_ref<constant>( arg );
				total += macro_preprocessor::eval_constant( context, cons );
			} );
			return macro_preprocessor::double_to_constant( context, total );
		}

		static object_ptr minus( reader_context& context, vector<object_ptr>& args )
		{
			double total = 0;
			bool first = true;
			for_each( args.begin(), args.end(), [&] 
			(object_ptr arg )
			{
				constant& cons = object_traits::cast_ref<constant>( arg );
				auto val = macro_preprocessor::eval_constant( context, cons );
				if ( first )
				{
					first = false;
					total = val;
				}
				else
				{
					total -= val;
				}
			} );
			return macro_preprocessor::double_to_constant( context, total );
		}
		static object_ptr less_than( reader_context& context, vector<object_ptr>& args )
		{
			double total = 0;
			if ( args.size() != 2 ) 
				throw runtime_error( "invalid < arguments, must be two" );
			double lhs = macro_preprocessor::eval_constant( context, object_traits::cast_ref<constant>( args[0] ) );
			double rhs = macro_preprocessor::eval_constant( context, object_traits::cast_ref<constant>( args[1] ) );
			if ( lhs < rhs ) total = 1;
			return macro_preprocessor::double_to_constant( context, total );
		}

		static object_ptr greater_than( reader_context& context, vector<object_ptr>& args )
		{
			double total = 0;
			if ( args.size() != 2 ) 
				throw runtime_error( "invalid > arguments, must be two" );
			double lhs = macro_preprocessor::eval_constant( context, object_traits::cast_ref<constant>( args[0] ) );
			double rhs = macro_preprocessor::eval_constant( context, object_traits::cast_ref<constant>( args[1] ) );
			if ( lhs > rhs ) total = 1;
			return macro_preprocessor::double_to_constant( context, total );
		}

		static object_ptr get_type( reader_context& /*context*/, vector<object_ptr>& args )
		{
			if ( args.size() != 1 ) throw runtime_error( "invalid number of args for type function" );
			if ( args[0]->type() == types::symbol )
			{
				symbol& value = object_traits::cast_ref<symbol>( args[0] );
				if( value._unevaled_type == nullptr ) throw runtime_error( "invalid type request, no type on symbol" );
				return value._unevaled_type;
			}
			else if ( args[0]->type() == types::constant )
			{
				constant& value = object_traits::cast_ref<constant>( args[0] );
				if ( value._unevaled_type == nullptr ) throw runtime_error( "constant provided with no type" );
				return value._unevaled_type;
			}
			throw runtime_error( "cannot grab type from anything but symbol or constant" );
		}

		static object_ptr set_type( reader_context& /*context*/, vector<object_ptr>& args )
		{
			if ( args.size() != 2 ) throw runtime_error( "invalid number of args for set type function" );
			cons_cell& new_type = object_traits::cast_ref<cons_cell>( args[1] );
			if ( args[0]->type() == types::symbol )
			{
				symbol& target = object_traits::cast_ref<symbol>( args[0] );
				target._unevaled_type = &new_type;
				target._evaled_type = nullptr;
			}
			else if ( args[0]->type() == types::constant )
			{
				constant& target = object_traits::cast_ref<constant>( args[0] );
				target._unevaled_type = &new_type;
			}
			else
				throw runtime_error( "cannot set type on anything but symbol or constant" );
			return args[0];
		}
		//what this really should do is:
		//1. parse the constant to what it will be when actually evaluated
		//2. static cast the constant to the desired final type.
		//3. write out the new constant to a string
		//4. create a new constant object with new string.
		static object_ptr create_constant( reader_context& context, vector<object_ptr>& args )
		{
			if ( args.size() != 2 ) throw runtime_error( "invalid number of args for create-constant" );
			constant& existing = object_traits::cast_ref<constant>( args[0] );
			cons_cell& request_type = object_traits::cast_ref<cons_cell>( args[1] );
			constant* retval = context._factory->create_constant();
			retval->_unparsed_number = existing._unparsed_number;
			retval->_unevaled_type = &request_type;
			return retval;
		}
		

		static object_ptr eval( reader_context& context, vector<object_ptr> args )
		{
			if ( args.size() != 1 ) throw runtime_error( "invalid number of args for eval" );
			return macro_preprocessor::lisp_eval( context, args[0] );
		}

		static void register_functions( string_table_ptr st, string_lisp_evaluator_map& map )
		{
			map.insert( make_pair( st->register_str( "+" ), make_shared<generic_preprocessor_function>( 
				[] ( reader_context& context, vector<object_ptr>& args ) { return plus( context, args ); } ) ) );
			map.insert( make_pair( st->register_str( "-" ), make_shared<generic_preprocessor_function>( 
				[] ( reader_context& context, vector<object_ptr>& args ) { return minus( context, args ); } ) ) );
			map.insert( make_pair( st->register_str( "<" ), make_shared<generic_preprocessor_function>( 
				[] ( reader_context& context, vector<object_ptr>& args ) { return less_than( context, args ); } ) ) );
			map.insert( make_pair( st->register_str( ">" ), make_shared<generic_preprocessor_function>( 
				[] ( reader_context& context, vector<object_ptr>& args ) { return greater_than( context, args ); } ) ) );
			map.insert( make_pair( st->register_str( "get-type" ), make_shared<generic_preprocessor_function>( 
				[] ( reader_context& context, vector<object_ptr>& args ) { return get_type( context, args ); } ) ) );
			map.insert( make_pair( st->register_str( "set-type" ), make_shared<generic_preprocessor_function>( 
				[] ( reader_context& context, vector<object_ptr>& args ) { return set_type( context, args ); } ) ) );
			map.insert( make_pair( st->register_str( "create-constant" ), make_shared<generic_preprocessor_function>( 
				[] ( reader_context& context, vector<object_ptr>& args ) { return create_constant( context, args ); } ) ) );
			map.insert( make_pair( st->register_str( "eval" ), make_shared<generic_preprocessor_function>( 
				[] ( reader_context& context, vector<object_ptr>& args ) { return eval( context, args ); } ) ) );
		}
	};

	struct macro_function : public lisp_evaluator
	{
		object_ptr_buffer	_arg_decls;
		cons_cell&			_body;
		macro_function( object_ptr_buffer decls, const cons_cell& b )
			: _arg_decls( decls )
			, _body( const_cast< cons_cell& >( b ) )
		{
		}

		virtual lisp::object_ptr eval( reader_context& context, cons_cell& cell )
		{
			vector<object_ptr> arg_vals = macro_preprocessor::eval_fn_arguments( context, cell );
			string_obj_ptr_map old_symbols( context._preprocessor_symbols );
			context._preprocessor_symbols.clear();
			preprocess_symbol_context preprocess( context._preprocessor_symbols );
			uint32_t arg_idx = 0;
			for_each( _arg_decls.begin(), _arg_decls.end(), [&]
			( object_ptr decl )
			{
				if ( arg_idx < arg_vals.size() )
				{
					auto arg_value = arg_vals[arg_idx];
					symbol& decl_name = object_traits::cast_ref<symbol>( decl );
					preprocess.add_symbol( decl_name._name, *arg_value );
				}
				++arg_idx;
			} );
			object_ptr retval( nullptr );
			for( cons_cell* body_cell = &_body; body_cell
				; body_cell = object_traits::cast<cons_cell>( body_cell->_next ) )
			{
				retval = macro_preprocessor::lisp_eval( context, body_cell->_value );
			}
			context._preprocessor_symbols = old_symbols;
			return retval;
		}
	};

	struct macro_function_plugin : public compiler_plugin
	{
		macro_function_plugin( string_table_ptr st )
			: compiler_plugin( st->register_str( "define macro function" ) )
		{
		}

		virtual ast_node& type_check( reader_context& context, cons_cell& callsite )
		{
			cons_cell& name_cell = object_traits::cast_ref<cons_cell>( callsite._next );
			cons_cell& args_cell = object_traits::cast_ref<cons_cell>( name_cell._next );
			cons_cell& body_cell = object_traits::cast_ref<cons_cell>( args_cell._next );
			symbol& name = object_traits::cast_ref<symbol>( name_cell._value );
			object_ptr_buffer arg_list = object_traits::cast_ref<array>( args_cell._value )._data;
			lisp_evaluator_ptr eval_ptr = make_shared<macro_function>( arg_list, body_cell );
			context._preprocessor_evaluators.insert( make_pair( name._name, eval_ptr ) );
			return *context._ast_allocator->construct<fake_ast_node>( context._string_table, context._type_library );
		}
	};

	struct template_fn
	{
		string_table_str			_name;
		type_ref_ptr				_rettype;
		object_ptr_buffer			_template_args;
		object_ptr_buffer			_fn_args;
		cons_cell*					_fn_body;

		template_fn() : _rettype( nullptr),  _fn_body( nullptr ) {}
		

		string_table_str get_generic_argument_name_from_arg_idx( size_t arg_idx )
		{
			if ( arg_idx >= _fn_args.size() )
				throw runtime_error( "invalid argument index" );

			symbol& fn_arg = object_traits::cast_ref<symbol>( _fn_args[arg_idx] );
			if ( fn_arg._evaled_type == nullptr )
				throw runtime_error( "invalid argument type" );
			type_ref& fn_arg_type( *fn_arg._evaled_type );
			if ( fn_arg_type._specializations.size() ) 
				return string_table_str();
			auto iter = find_if( _template_args.begin(), _template_args.end(), [&]
			( object_ptr item )
			{
				symbol* item_sym = object_traits::cast<symbol>( item );
				if ( item_sym )
					return item_sym->_name == fn_arg_type._name;
				return false;
			} );
			if ( iter != _template_args.end() )
				return object_traits::cast_ref<symbol>( *iter )._name;
			return string_table_str();
		}

		bool is_generic_argument( size_t arg_idx )
		{
			return get_generic_argument_name_from_arg_idx( arg_idx ).empty() == false;
		}
		
		uint32_t weight( data_buffer<type_ref_ptr> arg_types )
		{
			if ( arg_types.size() != _fn_args.size() )
				return 0;

			uint32_t generic_arg_count = 0;
			for( size_t idx = 0, end = arg_types.size(); idx < end; ++idx )
			{
				if ( is_generic_argument( idx ) )
					++generic_arg_count;
				else
				{
					symbol& arg_def = object_traits::cast_ref<symbol>( _fn_args[idx] );
					//non-generic types do not match, so this function cannot match
					if ( arg_types[idx] != arg_def._evaled_type )
						return 0;
				}
			}
			//attempt to match the fewest generic arguments.
			uint32_t inverse_generic_arg_count = _fn_args.size() - generic_arg_count;
			return inverse_generic_arg_count;
		}
	};

	struct template_fn_preprocessor : public compiler_plugin
	{
		template_fn							_base_fn;
		vector<template_fn>					_specializations;
		static const char* static_type() { return "template fn instance"; }
		template_fn_preprocessor( string_table_ptr st, template_fn base_fn ) 
			: compiler_plugin( st->register_str( static_type() ) )
			, _base_fn( base_fn ) 
		{}

		symbol& create_symbol( reader_context& context, const char* st, cons_cell* t = nullptr )
		{
			symbol* retval = context._factory->create_symbol();
			retval->_name = context._string_table->register_str( st );
			retval->_unevaled_type = t;
			return *retval;
		}
		cons_cell* append_cell(reader_context& context, cons_cell& last_cell )
		{
			auto c = context._factory->create_cell();
			last_cell._next = c;
			return c;
		}
		
		typedef unordered_map<string_table_str, cons_cell*> string_type_map;
		cons_cell& subst_type( reader_context& context, cons_cell& src_type, string_type_map& map )
		{
			type_ref& evaled_src = context._type_evaluator( src_type );
			if ( evaled_src._specializations.size() )
				return src_type;
			auto subst_iter = map.find( evaled_src._name );
			if ( subst_iter != map.end() )
				return *subst_iter->second;
			return src_type;
		}

		symbol& convert_symbol( reader_context& context, string_type_map& map, symbol& src_symbol )
		{
			cons_cell* dst_type = src_symbol._unevaled_type ? &subst_type( context, *src_symbol._unevaled_type, map ) : nullptr;
			return create_symbol( context, src_symbol._name.c_str(), dst_type );
		}

		cons_cell& convert_cell( reader_context& context, string_type_map& template_arg_vals, cons_cell& src_cell )
		{
			cons_cell* retval = context._factory->create_cell();
			retval->_value = convert_value( context, template_arg_vals, src_cell._value );
			if ( src_cell._next )
				retval->_next = convert_value( context, template_arg_vals, src_cell._next );
			return *retval;
		}

		array& convert_array( reader_context& context, string_type_map& template_arg_vals, array& src_cell )
		{
			array* retval = context._factory->create_array();
			retval->_data = context._factory->allocate_obj_buffer( src_cell._data.size() );
			for( size_t idx = 0, end = src_cell._data.size(); idx < end; ++idx )
			{
				retval->_data[idx] = convert_value( context, template_arg_vals, src_cell._data[idx] );
			}
			return *retval;
		}

		object_ptr convert_value( reader_context& context, string_type_map& template_arg_vals, object_ptr src )
		{
			if ( !src ) return nullptr;
			switch( src->type() )
			{
			case types::symbol:
				return &convert_symbol( context, template_arg_vals, object_traits::cast_ref<symbol>( src ) );
			case types::constant:
				return src;
			case types::cons_cell:
				return &convert_cell( context, template_arg_vals, object_traits::cast_ref<cons_cell>( src ) );
			case types::array:
				return &convert_array( context, template_arg_vals, object_traits::cast_ref<array>( src ) );
			default: break;
			}
			throw runtime_error( "Unrecognized object in template specialization::convert value" );
		}

		pair<symbol*,array*> uneval_type_to_parts( reader_context& context, type_ref& type )
		{
			symbol* type_name = context._factory->create_symbol();
			type_name->_name = type._name;
			array* specs = nullptr;
			if ( type._specializations.size() )
			{
				vector<object_ptr> spec_data;
				specs = context._factory->create_array();
				for_each( type._specializations.begin(), type._specializations.end(), [&]
				( type_ref_ptr tr )
				{
					auto unevaled_data = uneval_type_to_parts( context, *tr );
					spec_data.push_back( unevaled_data.first );
					if ( unevaled_data.second )
						spec_data.push_back( unevaled_data.second );
				} );
				specs->_data = context._factory->allocate_obj_buffer( spec_data.size() );
				memcpy( specs->_data.begin(), &spec_data[0], spec_data.size() * sizeof( object_ptr ) );
			}
			return make_pair( type_name, specs );
		}

		cons_cell* uneval_type( reader_context& context, type_ref& type )
		{
			auto parts = uneval_type_to_parts( context, type );
			cons_cell* retval = context._factory->create_cell();
			retval->_value = parts.first;
			if ( parts.second )
			{
				cons_cell* next_cell = context._factory->create_cell();
				retval->_next = next_cell;
				next_cell->_value = parts.second;
			}
			return retval;
		}

		pair<string, string_type_map> generate_template_fn_name( reader_context& context, symbol& fn_name
													, vector<ast_node_ptr>& arg_nodes, template_fn& spec )
		{
			//First, figure out the 
			string_type_map template_arg_vals;
			//TODO - use specializations indicated at callsite via fn name in addition to argument types
			//w/ argument types taking precedence.
			for( size_t arg_idx = 0, arg_end = arg_nodes.size(); arg_idx < arg_end; ++arg_idx )
			{
				ast_node_ptr arg = arg_nodes[arg_idx];
				auto arg_name = spec.get_generic_argument_name_from_arg_idx( arg_idx );
				if ( arg_name.empty() == false )
				{
					auto inserter = template_arg_vals.insert( make_pair( arg_name, uneval_type( context, arg->type() ) ) );
					if ( inserter.second == false )
					{
						if ( &context._type_evaluator(*inserter.first->second)  != &arg->type() )
							throw runtime_error( "invalid template instantiation; argument types do not match" );
					}
				}} 
			if ( template_arg_vals.size() != spec._template_args.size() )
				throw runtime_error( "incomplete template specialization; not enough args provided" );
			
			string name_mangle(fn_name._name.c_str());
			//We have to append the various types and such to the name
			//because not all of the template argument types are going to be
			//represented in the arguments to the instantiation.
			if ( spec._template_args.size() )
			{
				name_mangle.append( "|[" );
				bool first = true;
				for_each( spec._template_args.begin(), spec._template_args.end(), [&]
				(object_ptr arg )
				{
					if ( !first )
						name_mangle.append( " " );
					symbol& arg_sym = object_traits::cast_ref<symbol>( arg );
					auto runtime_type = template_arg_vals.find( arg_sym._name );
					if ( runtime_type == template_arg_vals.end() ) throw runtime_error( "invalid template type" );
					name_mangle.append( context._type_evaluator(*runtime_type->second).to_string() );
				} );
				name_mangle.append( "]" );
			}
			return make_pair( name_mangle, template_arg_vals );
		}

		virtual ast_node& handle_template_fn( reader_context& context, pair<string, string_type_map>& name_info, template_fn& spec )
		{
			string& name_mangle = name_info.first;
			string_type_map& template_arg_vals = name_info.second;
			cons_cell* cell = context._factory->create_cell();
			cons_cell* first_cell = cell;
			cell->_value = &create_symbol( context, "defn" );
			cell = append_cell( context, *cell );
			string_table_str name = context._string_table->register_str( name_mangle.c_str() );
			cell->_value = &create_symbol( context, name_mangle.c_str(), &subst_type( context, *uneval_type( context, *spec._rettype ), template_arg_vals ) );
			cell = append_cell( context, *cell );
			array& array_data = *context._factory->create_array();
			cell->_value = &array_data;
			array_data._data = context._factory->allocate_obj_buffer( spec._fn_args.size() );
			{
				size_t arg_idx = 0;
				for_each( spec._fn_args.begin(), spec._fn_args.end(), [&]
				( object_ptr spec_arg )
				{
					symbol& old_symbol = object_traits::cast_ref<symbol>( spec_arg );
					array_data._data[arg_idx] = &convert_symbol( context, template_arg_vals, old_symbol );
					++arg_idx;
				} );
			}
			cell->_next = &convert_cell( context, template_arg_vals, *spec._fn_body );
			auto iter = context._top_level_special_forms->find( context._string_table->register_str( "defn" ) );
			if ( iter == context._top_level_special_forms->end() )
				throw runtime_error( "odd error, no defn special form" );
			return iter->second->type_check( context, *first_cell );
		}

		virtual ast_node& type_check( reader_context& context, cons_cell& callsite )
		{
			symbol& fn_name = object_traits::cast_ref<symbol>( callsite._value );
			cons_cell& first_arg = object_traits::cast_ref<cons_cell>( callsite._next );
			vector<type_ref_ptr> arg_types;
			vector<ast_node_ptr> arg_nodes;
			
			// TODO - use combination of arguments passed in via function name type/type specializations
			// and arg nodes to figure out type substitions.
			for ( cons_cell* arg = &first_arg; arg; arg = object_traits::cast<cons_cell>( arg->_next ) )
			{
				ast_node& theNode = context._type_checker( arg->_value );
				arg_types.push_back( &theNode.type() );
				arg_nodes.push_back( &theNode );
			}
			type_ref& fn_type = context._type_library->get_type_ref( fn_name._name, arg_types );
			//Check for existing function overload
			auto node_iter = context._symbol_map->find( &fn_type );
			if ( node_iter != context._symbol_map->end() ) 
				return node_iter->second->apply( context, arg_nodes );

			template_fn*	best_spec = nullptr;
			uint32_t		spec_weight = 0;
			//How to weight these specializations
			for_each( _specializations.begin(), _specializations.end(), [&]
			( template_fn& spec )
			{
				auto item_weight = spec.weight( arg_types );
				if ( item_weight > spec_weight ) {
					best_spec = &spec;
					spec_weight = item_weight;
				}
			} );
			template_fn chosen_item = best_spec != nullptr ? *best_spec: _base_fn;

			pair<string,string_type_map> template_fn_name = generate_template_fn_name( context, fn_name, arg_nodes, chosen_item );
			type_ref& template_fn_type = context._type_library->get_type_ref( context._string_table->register_str( template_fn_name.first.c_str() ), arg_types );

			node_iter = context._symbol_map->find( &template_fn_type );
			if ( node_iter != context._symbol_map->end() )
				return node_iter->second->apply( context, arg_nodes );

			ast_node& provider_node = handle_template_fn( context, template_fn_name, chosen_item );
			//This should have been done by the generation of the function, but we have to be sure.
			context._symbol_map->insert( make_pair( &template_fn_type, &provider_node ) );
			context._additional_top_level_nodes.push_back( &provider_node );

			return provider_node.apply( context, arg_nodes );
		}
	};

	class template_fn_plugin : public compiler_plugin
	{
	public:
		const char* static_plugin_name() { return "template function definition"; }
		template_fn_plugin( string_table_ptr st ) : compiler_plugin( st->register_str( static_plugin_name() ) ) {}

		template_fn type_check_function( reader_context& context, cons_cell& cell )
		{
			cons_cell& template_arg_cell = object_traits::cast_ref<cons_cell>( cell._next );
			object_ptr_buffer template_args = object_traits::cast_ref<array>( template_arg_cell._value )._data;
			cons_cell& name_cell = object_traits::cast_ref<cons_cell>( template_arg_cell._next );
			symbol& name = object_traits::cast_ref<symbol>( name_cell._value );
			cons_cell& function_arg_cell = object_traits::cast_ref<cons_cell>( name_cell._next );
			object_ptr_buffer function_args = object_traits::cast_ref<array>( function_arg_cell._value )._data;
			cons_cell& body_cell = object_traits::cast_ref<cons_cell>( function_arg_cell._next );
			template_fn retval;
			retval._rettype = &context.symbol_type( name );
			for_each( function_args.begin(), function_args.end(), [&]
			(object_ptr arg )
			{
				symbol& arg_sym = object_traits::cast_ref<symbol>( arg );
				context.symbol_type( arg_sym );
			} );
			retval._template_args = template_args;
			retval._fn_args = function_args;
			retval._fn_body = &body_cell;
			retval._name = name._name;
			return retval;
		}

		void type_check_def( reader_context& context, cons_cell& cell )
		{
			template_fn new_fn = type_check_function( context, cell );
			compiler_plugin_ptr plugin = make_shared<template_fn_preprocessor>( context._string_table, new_fn );
			context._special_forms->insert( make_pair( new_fn._name, plugin ) );
		}

		void type_check_specialization( reader_context& context, cons_cell& cell )
		{
			template_fn new_fn = type_check_function( context, cell );
			auto iter = context._special_forms->find( new_fn._name );
			if ( iter == context._special_forms->end() ) throw runtime_error( "unable to find template to specialize" );
			if ( iter->second->plugin_name() 
				!= context._string_table->register_str( template_fn_preprocessor::static_type() ) )
				throw runtime_error( "invalid template specialization" );
			auto item_ptr = static_cast<template_fn_preprocessor*>( iter->second.get() );
			item_ptr->_specializations.push_back( new_fn );
		}
		
		ast_node& type_check( reader_context& context, cons_cell& cell )
		{
			symbol& command = object_traits::cast_ref<symbol>( cell._value );
			if ( command._name == context._string_table->register_str( "define-template-fn" ) )
			{
				type_check_def( context, cell );
			}
			else
			{
				type_check_specialization( context, cell );
			}
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
		
		virtual bool executable_statement() const { return true; }
		
		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			auto symbol_iter = context._variables.find( _symbol._name );
			if ( symbol_iter == context._variables.end() ) throw runtime_error( "missing symbol" );
			if ( &type() == &context._type_library->get_void_type() )
				return make_pair( llvm_value_ptr_opt(), &type() );
			return make_pair( context._builder.CreateLoad( symbol_iter->second.first.get() ), &type() );
		}
	};
	
	struct resolution_ast_node : public ast_node
	{
		symbol_resolution_context_ptr _resolution;
		resolution_ast_node( string_table_ptr st, symbol_resolution_context_ptr res )
			: ast_node( st->register_str( "resolution node" ), res->resolved_type() )
			, _resolution( res )
		{
		}
		
		virtual bool executable_statement() const { return true; }

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			return make_pair( _resolution->load( context, data_buffer<llvm_value_ptr>() ), &_resolution->resolved_type());
		}
	};

	struct numeric_constant_ast_node : public ast_node
	{
		uint8_t*	_data;
		numeric_constant_ast_node( string_table_ptr st, uint8_t* data, const type_ref& dtype )
			: ast_node( st->register_str( "numeric constant" ), dtype )
			, _data( data )
		{
		}
		
		virtual bool executable_statement() const { return true; }

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			switch( context._type_library->to_base_numeric_type( type() ) )
			{
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name )		\
			case base_numeric_types::name:			\
				return make_pair( llvm_helper::llvm_constant_map<base_numeric_types::name>::parse( _data )	\
								, &type() );
				CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
			default: break;
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
		
		virtual bool executable_statement() const { return true; }

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			ast_node& cond_node = children().front();
			ast_node& true_node = *cond_node.next_node();
			ast_node& false_node = *true_node.next_node();
			auto cond_result = cond_node.compile_second_pass( context );
			
			Function *theFunction = context._builder.GetInsertBlock()->getParent();
			
			BasicBlock *ThenBB = BasicBlock::Create(getGlobalContext(), "then", theFunction);

			BasicBlock *ElseBB = BasicBlock::Create(getGlobalContext(), "else");

			BasicBlock *MergeBB = BasicBlock::Create(getGlobalContext(), "ifcont");
			context._builder.CreateCondBr( cond_result.first.get(), ThenBB, ElseBB );
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
			if ( true_result.second != &context._type_library->get_void_type() )
			{
				PHINode *PN = context._builder.CreatePHI( context.type_ref_type(*true_result.second ).get(), 2,
											"iftmp");
  
				PN->addIncoming(true_result.first.get(), ThenBB);
				PN->addIncoming(false_result.first.get(), ElseBB);
				return make_pair(PN, true_result.second);
			}
			return make_pair( llvm_value_ptr_opt(), true_result.second );
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
			ast_node& cond_node = context._type_checker( cond_cell._value );
			ast_node& true_node = context._type_checker( true_cell._value );
			ast_node& false_node = context._type_checker( false_cell._value );
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

	struct let_ast_node : public ast_node
	{
		vector<pair<symbol*, ast_node*> > _let_vars;
		let_ast_node( string_table_ptr st, const type_ref& type )
			: ast_node( st->register_str( "let statement" ), type )
		{
		}
		
		virtual bool executable_statement() const { return true; }

		static void initialize_assign_block( compiler_context& context
												, vector<pair<symbol*, ast_node*> >& vars
												, variable_context& let_vars )
		{
			Function* theFunction = context._builder.GetInsertBlock()->getParent();
			IRBuilder<> entryBuilder( &theFunction->getEntryBlock(), theFunction->getEntryBlock().begin() );
			
			for_each( vars.begin(), vars.end(), [&]
			( pair<symbol*, ast_node*>& var_dec )
			{
				auto var_eval = var_dec.second->compile_second_pass( context );
				if( var_eval.first )
				{
					auto alloca = entryBuilder.CreateAlloca( context.type_ref_type( *var_eval.second ).get()
																	, 0, var_dec.first->_name.c_str() );
					context._builder.CreateStore( var_eval.first.get(), alloca );
					let_vars.add_variable( var_dec.first->_name, alloca, *var_eval.second );
				}
				else
					let_vars.add_variable( var_dec.first->_name, nullptr, context._type_library->get_void_type() );
			} );
		}
		
		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			variable_context let_vars( context._variables );
			initialize_assign_block( context, _let_vars, let_vars );

			pair<llvm_value_ptr_opt, type_ref_ptr> retval;
			for ( auto iter = children().begin(), end = children().end(); iter != end; ++iter )
			{
				retval = iter->compile_second_pass( context );
			}
			return retval;
		}
	};

	struct let_compiler_plugin : public compiler_plugin
	{
		let_compiler_plugin( string_table_ptr st )
			: compiler_plugin( st->register_str( "let compiler plugin" ) )
		{
		}

		virtual ast_node& type_check( reader_context& context, lisp::cons_cell& cell )
		{
			cons_cell& array_cell = object_traits::cast_ref<cons_cell>( cell._next );
			cons_cell& body_start = object_traits::cast_ref<cons_cell>( array_cell._next );
			object_ptr_buffer assign = object_traits::cast_ref<array>( array_cell._value )._data;
			vector<pair<symbol*,ast_node*> > let_vars;
			symbol_type_context let_check_context( context._context_symbol_types );
			for ( size_t idx = 0, end = assign.size(); idx < end; idx = idx + 2 )
			{
				symbol& var_name = object_traits::cast_ref<symbol>( assign[idx] );
				ast_node& var_expr = context._type_checker( assign[idx+1] );
				let_vars.push_back( make_pair( &var_name, &var_expr ) );
				let_check_context.add_symbol( var_name._name, var_expr.type() );
			}
			vector<ast_node*> body_nodes;
			for ( cons_cell* body_cell = &body_start; body_cell
				; body_cell = object_traits::cast<cons_cell>( body_cell->_next ) )
			{
				body_nodes.push_back( &context._type_checker( body_cell->_value ) );
			}
			//TODO change this to null so the system understand null or nil.
			if( body_nodes.empty() )
				throw runtime_error( "invalid let statement" );
			let_ast_node* new_node 
				= context._ast_allocator->construct<let_ast_node>( context._string_table, body_nodes.back()->type() );
			new_node->_let_vars = let_vars;
			for_each( body_nodes.begin(), body_nodes.end(), [&]
			( ast_node* node )
			{
				new_node->children().push_back( *node );
			} );
			return *new_node;
		}
	};

	struct pod_def_ast_node : public ast_node, public llvm_function_provider
	{
		vector<symbol*>	_fields;
		Function*		_function;
		pod_def_ast_node( string_table_ptr st, const type_ref& type )
			: ast_node( st->register_str( "pod definition" ), type )
			, _function( nullptr )
		{
		}
		//Called to allow the ast node to resolve the rest of a symbol when the symbol's first item pointed
		//to a variable if this node type.  Used for struct lookups of the type a.b
		virtual void resolve_symbol( reader_context& context
											, string_table_str split_symbol
											, symbol_resolution_context& resolution_context )
		{
			auto find_result = find_if( _fields.begin(), _fields.end(), [&]
			( symbol* sym ) { return sym->_name == split_symbol; } );
			if ( find_result == _fields.end() ) throw runtime_error( "enable to result symbol" );
			if ( (*find_result)->_evaled_type != &context._type_library->get_void_type() )
			{
				uint32_t find_idx = static_cast<uint32_t>( find_result - _fields.begin() );
				resolution_context.add_GEP_index( find_idx, *(*find_result)->_evaled_type );
			}
		}

		//compiler-created constructor
		virtual ast_node& apply( reader_context& context, data_buffer<ast_node_ptr> args )
		{
			function_call_node* new_node 
				= context._ast_allocator->construct<function_call_node>( context._string_table, *this );
			for_each( args.begin(), args.end(), [&]
			( ast_node_ptr arg )
			{
				new_node->children().push_back( *arg );
			} );
			return *new_node;
		}

		virtual void compile_first_pass(compiler_context& context)
		{
			vector<llvm_type_ptr> arg_types;
			for_each( _fields.begin(), _fields.end(), [&]
			( symbol* field )
			{
				if ( field->_evaled_type != &context._type_library->get_void_type() )
					arg_types.push_back( context.type_ref_type( *field->_evaled_type ).get() );
			} );
			//Create struct type definition to llvm.
			llvm_type_ptr struct_type = StructType::create( getGlobalContext(), arg_types );
			context._type_map.insert( make_pair( &type(), struct_type ) );
			FunctionType* fn_type = FunctionType::get( struct_type, arg_types, false );
			_function = Function::Create( fn_type, GlobalValue::ExternalLinkage, "", &context._module );
		}
		
		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			variable_context vcontext( context._variables );
			function_def_node::initialize_function( context, *_function, _fields, vcontext );
			IRBuilder<> entryBuilder( &_function->getEntryBlock(), _function->getEntryBlock().begin() );
			Type* struct_type = context.type_ref_type( type() ).get();
			auto struct_ptr = entryBuilder.CreateAlloca( struct_type, 0 );
			uint32_t idx = 0;
			for_each( _fields.begin(), _fields.end(), [&]
			( symbol* field )
			{
				if ( field->_evaled_type != &context._type_library->get_void_type() )
				{
					Value* val_ptr = context._builder.CreateConstInBoundsGEP2_32( struct_ptr, 0, idx, field->_name.c_str() );
					Value* arg_val = context._builder.CreateLoad( context._variables.find( field->_name )->second.first.get() );
					context._builder.CreateStore( arg_val, val_ptr );
				}
				++idx;
			} );
			auto struct_val = context._builder.CreateLoad( struct_ptr );
			context._builder.CreateRet( struct_val );
			verifyFunction(*_function );
			context._fpm.run( *_function );
			return make_pair( struct_val, &type() );
		}
		
		virtual type_ref& return_type() { return type(); }
		virtual Function& function() 
		{
			if ( _function == nullptr )
				throw runtime_error( "podtype first pass not called yet" );
			return *_function;
		}
	};

	struct pod_def_compiler_plugin : public compiler_plugin
	{
		pod_def_compiler_plugin( string_table_ptr st )
			: compiler_plugin( st->register_str( "pod definition" ) )
		{
		}
		
		virtual ast_node& type_check( reader_context& context, lisp::cons_cell& cell )
		{
			cons_cell& name_cell = object_traits::cast_ref<cons_cell>( cell._next );
			symbol& name = object_traits::cast_ref<symbol>( name_cell._value );
			cons_cell& field_start = object_traits::cast_ref<cons_cell>( name_cell._next );
			vector<symbol*> fields;
			vector<type_ref_ptr> fn_arg_type;
			for( cons_cell* field_cell = &field_start; field_cell
				; field_cell = object_traits::cast<cons_cell>( field_cell->_next ) )
			{
				fields.push_back( &object_traits::cast_ref<symbol>( field_cell->_value ) );
				fn_arg_type.push_back( &context.symbol_type( *fields.back() ) );
			}
			//type assigned to pod.
			type_ref& struct_type = context._type_library->get_type_ref( name._name );
			//type assigned to constructor.
			type_ref& construct_type = context._type_library->get_type_ref( name._name, fn_arg_type );
			pod_def_ast_node* new_node 
				= context._ast_allocator->construct<pod_def_ast_node>( context._string_table, struct_type );
			new_node->_fields = fields;
			context._symbol_map->insert( make_pair( &struct_type, new_node ) );
			context._symbol_map->insert( make_pair( &construct_type, new_node ) );
			return *new_node;
		}
	};

	struct for_loop_ast_node : public ast_node
	{
		vector<pair<symbol*, ast_node*> >	_for_vars;
		ast_node_ptr						_cond_node;

		for_loop_ast_node( string_table_ptr st, type_library_ptr lt )
			: ast_node( st->register_str( "for loop" ), lt->get_void_type() )
		{
		}
		
		virtual bool executable_statement() const { return true; }

		
		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			variable_context let_vars( context._variables );
			let_ast_node::initialize_assign_block( context, _for_vars, let_vars ); 
			Function* theFunction = context._builder.GetInsertBlock()->getParent();
			
			BasicBlock* loop_update_block = BasicBlock::Create( getGlobalContext(), "loop update", theFunction );
			BasicBlock* cond_block = BasicBlock::Create( getGlobalContext(), "cond block", theFunction );
			BasicBlock* exit_block = BasicBlock::Create( getGlobalContext(), "exit block", theFunction );
			context._builder.CreateBr( cond_block );
			context._builder.SetInsertPoint( cond_block );
			llvm_value_ptr next_val = _cond_node->compile_second_pass( context ).first.get();
			context._builder.CreateCondBr( next_val, loop_update_block, exit_block );

			context._builder.SetInsertPoint( loop_update_block );
			//output looping
			for ( auto iter = children().begin(), end = children().end(); iter != end; ++iter )
			{
				iter->compile_second_pass( context );
			}
			context._builder.CreateBr( cond_block );
			context._builder.SetInsertPoint( exit_block );
			return pair<llvm_value_ptr_opt, type_ref_ptr>( nullptr, &context._type_library->get_void_type() );
		}
	};
	

	struct for_loop_plugin : public compiler_plugin
	{
		for_loop_plugin( string_table_ptr st ) : compiler_plugin( st->register_str( "for loop plugin" ) )
		{
		}
		virtual ast_node& type_check( reader_context& context, lisp::cons_cell& cell )
		{
			// for loop is an array of init statements
			// a boolean conditional, 
			// an array of update statements
			// and the rest is the for body.
			cons_cell& init_cell = object_traits::cast_ref<cons_cell>( cell._next );
			cons_cell& cond_cell = object_traits::cast_ref<cons_cell>( init_cell._next );
			cons_cell& update_cell = object_traits::cast_ref<cons_cell>( cond_cell._next );
			cons_cell& body_start = object_traits::cast_ref<cons_cell>( update_cell._next );
			object_ptr_buffer init_list = object_traits::cast_ref<array>( init_cell._value )._data;
			object_ptr_buffer update_list = object_traits::cast_ref<array>( update_cell._value )._data;
			vector<pair<symbol*, ast_node*> > init_nodes;
			symbol_type_context for_check_context( context._context_symbol_types );
			for ( size_t idx = 0, end = init_list.size(); idx < end; idx +=2  )
			{
				symbol& var_name = object_traits::cast_ref<symbol>( init_list[idx] );
				ast_node& var_expr = context._type_checker( init_list[idx+1] );
				init_nodes.push_back( make_pair( &var_name, &var_expr ) );
				for_check_context.add_symbol( var_name._name, var_expr.type() );
			}
			ast_node& cond_node = context._type_checker( cond_cell._value );
			if ( context._type_library->to_base_numeric_type( cond_node.type() )
				!= base_numeric_types::i1 )
				throw runtime_error( "invalid for statement; condition not boolean" );
			vector<ast_node*> body_nodes;
			for ( cons_cell* body_cell = &body_start; body_cell
					; body_cell = object_traits::cast<cons_cell>( body_cell->_next ) )
			{
				body_nodes.push_back( &context._type_checker( body_cell->_value ) );
			}
			for_each ( update_list.begin(), update_list.end(), [&]
			( object_ptr item )
			{
				body_nodes.push_back( &context._type_checker( item ) );
			} );
			for_loop_ast_node* new_node 
				= context._ast_allocator->construct<for_loop_ast_node>( context._string_table, context._type_library );
			new_node->_for_vars = init_nodes;
			new_node->_cond_node = &cond_node;
			for_each( body_nodes.begin(), body_nodes.end(), [&]
			( ast_node_ptr node )
			{
				new_node->children().push_back( *node );
			} );
			return *new_node;
		}
	};


	struct set_ast_node : public ast_node
	{
		symbol_resolution_context_ptr _resolve_context;
		set_ast_node( string_table_ptr st, symbol_resolution_context_ptr sr )
			: ast_node( st->register_str( "set node" ), sr->resolved_type() )
			, _resolve_context( sr )
		{
		}
		
		virtual bool executable_statement() const { return true; }

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass( compiler_context& context )
		{
			pair<llvm_value_ptr_opt, type_ref_ptr> assign_var (nullptr, nullptr );
			vector<llvm_value_ptr> indexes;
			ast_node* last_child = nullptr;
			for ( auto iter = children().begin(), end = children().end(); iter != end; ++iter )
			{
				if ( last_child )
				{
					assign_var = last_child->compile_second_pass( context );
					indexes.push_back( assign_var.first.get() );
				}
				last_child = &(*iter);
			}
			if ( last_child == nullptr ) throw runtime_error( "invalid set ast node" );
			assign_var = last_child->compile_second_pass( context );
			if ( &type() != &context._type_library->get_void_type() )
			{
				_resolve_context->store( context, indexes, assign_var.first.get() );
			}
			return assign_var;
		}
	};


	struct set_plugin : public compiler_plugin
	{
		set_plugin( string_table_ptr st ) 
			: compiler_plugin( st->register_str( "set plugin" ) )
		{
		}

		virtual ast_node& type_check( reader_context& context, lisp::cons_cell& cell )
		{
			cons_cell& name_cell = object_traits::cast_ref<cons_cell>( cell._next );
			cons_cell& first_expr_cell = object_traits::cast_ref<cons_cell>( name_cell._next );
			
			symbol& name = object_traits::cast_ref<symbol>( name_cell._value );
			
			auto res_context = resolve_symbol( context, name );
			set_ast_node* retval 
				= context._ast_allocator->construct<set_ast_node>( context._string_table, res_context );

			type_ref_ptr item_type = &res_context->resolved_type();
			ast_node* last_expr_node = nullptr;
			for ( cons_cell* expr = &first_expr_cell; expr; expr = object_traits::cast<cons_cell>( expr->_next ) )
			{
				ast_node& new_node = context._type_checker( expr->_value );
				retval->children().push_back( new_node );
				last_expr_node = &new_node;
				if ( expr->_next )
				{
					auto num_type = context._type_library->to_base_numeric_type( new_node.type() );
					if ( base_numeric_types::is_int_type( num_type ) == false )
						throw runtime_error( "indexes into arrays must be integers" );
					item_type = &context._type_library->deref_ptr_type( *item_type );
				}
			}
			if ( last_expr_node == nullptr ) throw runtime_error( "invalid set: no expression" );
			if ( &last_expr_node->type() != item_type )
				throw runtime_error( "Invalid set: variable type and expression type do not match" );

			return *retval;
		}
	};

	struct get_ast_node : public ast_node
	{
		symbol_resolution_context_ptr _resolve_context;
		get_ast_node( string_table_ptr st, symbol_resolution_context_ptr sr, const type_ref& type )
			: ast_node( st->register_str( "set node" ), type )
			, _resolve_context( sr )
		{
		}
		
		virtual bool executable_statement() const { return true; }

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass( compiler_context& context )
		{
			if ( &type() == &context._type_library->get_void_type() )
				return pair<llvm_value_ptr_opt,type_ref_ptr>( nullptr, &type() );
			pair<llvm_value_ptr_opt, type_ref_ptr> assign_var (nullptr, nullptr );
			vector<llvm_value_ptr> indexes;
			for ( auto iter = children().begin(), end = children().end(); iter != end; ++iter )
			{
				assign_var = iter->compile_second_pass( context );
				indexes.push_back( assign_var.first.get() );
			}
			return make_pair( _resolve_context->load( context, indexes ), &type() );
		}
	};
	
	struct get_plugin : public compiler_plugin
	{
		get_plugin( string_table_ptr st ) 
			: compiler_plugin( st->register_str( "get plugin" ) )
		{
		}

		virtual ast_node& type_check( reader_context& context, lisp::cons_cell& cell )
		{
			cons_cell& name_cell = object_traits::cast_ref<cons_cell>( cell._next );
			cons_cell* first_expr_cell = object_traits::cast<cons_cell>( name_cell._next );
			symbol& name = object_traits::cast_ref<symbol>( name_cell._value );
			
			auto res_context = resolve_symbol( context, name );
			type_ref* resolved_type = &res_context->resolved_type();
			vector<ast_node_ptr> children;
			for ( cons_cell* expr = first_expr_cell; expr; expr = object_traits::cast<cons_cell>( expr->_next ) )
			{
				ast_node& new_node = context._type_checker( expr->_value );
				children.push_back( &new_node );
				auto num_type = context._type_library->to_base_numeric_type( new_node.type() );
				if ( base_numeric_types::is_int_type( num_type ) == false )
					throw runtime_error( "indexes into arrays must be integers" );
				resolved_type = &context._type_library->deref_ptr_type( *resolved_type );
			}
			get_ast_node* retval 
				= context._ast_allocator->construct<get_ast_node>( context._string_table, res_context, *resolved_type );

			for_each( children.begin(), children.end(), [&]
			( ast_node_ptr child )
			{
				retval->children().push_back( *child );
			} );

			return *retval;
		}
	};


	struct numeric_cast_node : public ast_node
	{
		numeric_cast_node( string_table_ptr st, const type_ref& t )
			: ast_node( st->register_str( "numeric cast node" ), t )
		{
		}
		
		virtual bool executable_statement() const { return true; }

		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass( compiler_context& context )
		{
			pair<llvm_value_ptr_opt, type_ref_ptr> arg_eval( nullptr, nullptr );
			for ( auto iter = children().begin(), end = children().end(); iter != end; ++iter )
			{
				arg_eval = iter->compile_second_pass( context );
			}
			if ( arg_eval.first.empty() ) throw runtime_error( "invalid numeric cast" );
			auto source = context._type_library->to_base_numeric_type( *arg_eval.second );
			auto target = context._type_library->to_base_numeric_type( type() );
			
			Type* target_type = context.type_ref_type( type() ).get();
			Value* retval = nullptr;
			Value* sourceval = arg_eval.first.get();
			if ( base_numeric_types::is_float_type( source ) )
			{
				if ( target == base_numeric_types::f64 )
					retval = context._builder.CreateFPExt( sourceval, target_type );
				else if ( target == base_numeric_types::f32 )
					retval = context._builder.CreateFPTrunc( sourceval, target_type );
				else if ( base_numeric_types::is_signed_int_type( target ) )
					retval = context._builder.CreateFPToSI( sourceval, target_type );
				else if ( base_numeric_types::is_unsigned_int_type( target ) )
					retval = context._builder.CreateFPToUI( sourceval, target_type );
			}
			else if ( base_numeric_types::is_int_type( source ) )
			{
				if ( base_numeric_types::is_float_type( target ) )
				{
					if ( base_numeric_types::is_signed_int_type( source ) )
						retval = context._builder.CreateSIToFP( sourceval, target_type );
					else
						retval = context._builder.CreateUIToFP( sourceval, target_type );
				}
				else 
				{
					if ( base_numeric_types::num_bits( source ) > base_numeric_types::num_bits( target ) )
					{
						retval = context._builder.CreateTrunc( sourceval, target_type );
					}
					else if ( base_numeric_types::num_bits( source ) < base_numeric_types::num_bits( target ) )
					{
						if ( base_numeric_types::is_signed_int_type( target ) )
							retval = context._builder.CreateSExt( sourceval, target_type );
						else
							retval = context._builder.CreateZExt( sourceval, target_type );
					}
					//types are same size, just do a bitcast of sorts.
					else
						retval = context._builder.CreateBitCast( sourceval, target_type );
				}
			}

			if ( retval == nullptr ) throw runtime_error( "cast error" );

			return make_pair( retval, &type() );
		}
	};

	
	struct numeric_cast_plugin : public compiler_plugin
	{
		numeric_cast_plugin( string_table_ptr st )
			: compiler_plugin( st->register_str( "numeric cast plugin" ) )
		{
		}

		static void check_valid_numeric_type( base_numeric_types::_enum t )
		{
			if ( t == base_numeric_types::no_known_type
				|| t == base_numeric_types::i1 )
				throw runtime_error( "invalid type for numeric cast" );
		}
		
		virtual ast_node& type_check( reader_context& context, lisp::cons_cell& cell )
		{
			symbol& my_name = object_traits::cast_ref<symbol>( cell._value );
			cons_cell& body_start = object_traits::cast_ref<cons_cell>( cell._next );
			type_ref& name_type = context.symbol_type( my_name );
			auto num_type = context._type_library->to_base_numeric_type( name_type );
			check_valid_numeric_type( num_type );
			numeric_cast_node* num_node 
				= context._ast_allocator->construct<numeric_cast_node>( context._string_table, name_type );
			ast_node_ptr last_node( nullptr );
			for ( cons_cell* body = &body_start; body; body = object_traits::cast<cons_cell>( body->_next ) )
			{
				last_node = &context._type_checker( body->_value );
				num_node->children().push_back( *last_node );
			}
			if ( last_node == nullptr ) throw runtime_error( "invalid numeric cast" );
			check_valid_numeric_type( context._type_library->to_base_numeric_type( last_node->type() ) );
			return *num_node;
		}
	};

	struct ptr_cast_node : public ast_node
	{
		ptr_cast_node( string_table_ptr st, const type_ref& t )
			: ast_node( st->register_str( "ptr cast node" ), t )
		{
		}

		virtual bool executable_statement() const { return true; }
		
		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass( compiler_context& context )
		{
			pair<llvm_value_ptr_opt, type_ref_ptr> src_data = children()._head->compile_second_pass( context );
			llvm_value_ptr result 
					= context._builder.CreateBitCast( src_data.first.get(), context.type_ref_type( type() ).get() );
			return make_pair( result, &type() );
		}
	};

	struct ptr_cast_plugin : public compiler_plugin
	{
		ptr_cast_plugin( string_table_ptr st )
			: compiler_plugin( st->register_str( "ptr cast plugin" ) )
		{
		}
		
		virtual ast_node& type_check( reader_context& context, lisp::cons_cell& cell )
		{
			symbol& my_name = object_traits::cast_ref<symbol>( cell._value );
			type_ref& dest_type = context.symbol_type( my_name );
			cons_cell& expr = object_traits::cast_ref<cons_cell>( cell._next );
			ast_node& src_data = context._type_checker( expr._value );
			type_ref& src_type( src_data.type() );
			if ( context._type_library->is_pointer_type( src_type ) == false )
				throw runtime_error( "Invalid ptr cast, src is not pointer types" );
			ptr_cast_node* retval = context._ast_allocator->construct<ptr_cast_node>( context._string_table
				, context._type_library->get_ptr_type( dest_type ) );
			retval->children().push_back( src_data );
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
		
		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& context)
		{
			ast_node& lhs = *children()._head;
			ast_node& rhs = *lhs.next_node();
			llvm_value_ptr lhs_value = lhs.compile_second_pass( context ).first.get();
			llvm_value_ptr rhs_value = rhs.compile_second_pass( context ).first.get();
			llvm_value_ptr retval = _function( context._builder, lhs_value, rhs_value );
			return make_pair( retval, &type() );
		}
	};


	struct global_function_ast_node : public ast_node, public llvm_function_provider
	{
		global_function_entry _entry;
		global_function_ast_node( string_table_ptr st, const global_function_entry& e )
			: ast_node( st->register_str( "global ast node" ), *e._ret_type )
			, _entry( e )
		{
		}
		
		virtual ast_node& apply( reader_context& context, data_buffer<ast_node_ptr> args )
		{
			function_call_node* new_node 
				= context._ast_allocator->construct<function_call_node>( context._string_table, *this );
			for_each( args.begin(), args.end(), [&]
			( ast_node_ptr arg )
			{
				new_node->children().push_back( *arg );
			} );
			return *new_node;
		}
		
		virtual type_ref& return_type() { return *_entry._ret_type; }
		virtual Function& function()
		{
			if( _entry._function == nullptr )
				throw runtime_error( "global node needs first pass compilation" );
			return *_entry._function;
		}

		virtual void compile_first_pass( compiler_context& context )
		{
			vector<Type*> fn_arg_types;
			for_each( _entry._fn_type->_specializations.begin(), _entry._fn_type->_specializations.end()
					, [&]
					( type_ref_ptr t )
			{
				if ( t != &context._type_library->get_void_type() )
					fn_arg_types.push_back( context.type_ref_type( *t ).get() );
			} );
			FunctionType* fn_type = FunctionType::get( context.type_ref_type( *_entry._ret_type ).get(), fn_arg_types, false );
			Function* fn_dev = Function::Create( fn_type, GlobalValue::ExternalLinkage
													, _entry._fn_type->_name.c_str(), &context._module );
			_entry._function = fn_dev;
			context._eng.addGlobalMapping( _entry._function, _entry._fn_entry );
		}
		
		virtual pair<llvm_value_ptr_opt, type_ref_ptr> compile_second_pass(compiler_context& /*context*/)
		{
			throw runtime_error( "second pass compilation not supported" );
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

	auto plugin_ptr_iter = context._special_forms->find( fn_name._name );
	if ( plugin_ptr_iter !=  context._special_forms->end() )
	{
		return plugin_ptr_iter->second->type_check( context, cell );
	}
	return type_check_function_application( context, cell );
}

ast_node& base_language_plugins::type_check_symbol( reader_context& context, lisp::symbol& sym )
{
	symbol& cell_symbol = sym;
	vector<string> parts = split_symbol( cell_symbol );
	if ( parts.size() == 1 )
	{
		//common case.
		symbol_type_ref_map::iterator symbol_type = context._context_symbol_types.find( cell_symbol._name );
		if ( symbol_type == context._context_symbol_types.end() ) throw runtime_error( "unresolved symbol" );
		return *context._ast_allocator->construct<symbol_ast_node>( context._string_table, cell_symbol
																	, *symbol_type->second );
	}
	else
	{
		auto res_context = resolve_symbol( context, sym );
		resolution_ast_node* retval 
			= context._ast_allocator->construct<resolution_ast_node>( context._string_table, res_context );
		return *retval;
	}
}

namespace
{
	template<typename num_type>
	struct str_to_num
	{
	};

	//Lots of things to do here.  First would be to allow compile time expressions as constants and not just numbers.
	//Second would be to allow number of different bases
	//third would be to have careful checking of ranges.
	template<> struct str_to_num<bool> { static bool parse( const string& val ) { return std::stol( val ) ? true : false; } };
	template<> struct str_to_num<uint8_t> { static uint8_t parse( const string& val ) { return static_cast<uint8_t>( std::stol( val ) ); } };
	template<> struct str_to_num<int8_t> { static int8_t parse( const string& val ) { return static_cast<int8_t>( std::stol( val ) ); } };
	template<> struct str_to_num<uint16_t> { static uint16_t parse( const string& val ) { return static_cast<uint16_t>( std::stol( val ) ); } };
	template<> struct str_to_num<int16_t> { static int16_t parse( const string& val ) { return static_cast<int16_t>( std::stol( val ) ); } };
	template<> struct str_to_num<uint32_t> { static uint32_t parse( const string& val ) { return static_cast<uint32_t>( std::stoll( val ) ); } };
	template<> struct str_to_num<int32_t> { static int32_t parse( const string& val ) { return static_cast<int32_t>( std::stoll( val ) ); } };
	template<> struct str_to_num<uint64_t> { static uint64_t parse( const string& val ) { return static_cast<uint64_t>( std::stoll( val ) ); } };
	template<> struct str_to_num<int64_t> { static int64_t parse( const string& val ) { return static_cast<int64_t>( std::stoll( val ) ); } };
	template<> struct str_to_num<float> { static float parse( const string& val ) { return static_cast<float>( std::stof( val ) ); } };
	template<> struct str_to_num<double> { static double parse( const string& val ) { return static_cast<double>( std::stod( val ) ); } };

	
	template<typename number_type>
	uint8_t* parse_constant_value( reader_context& context, const std::string& val )
	{
		number_type parse_val = str_to_num<number_type>::parse( val );
		uint8_t* retval = context._factory->allocate_data( sizeof( number_type ), sizeof( number_type ) );
		memcpy( retval, &parse_val, sizeof( number_type ) );
		return retval;
	}
}

ast_node& base_language_plugins::type_check_numeric_constant( reader_context& context, lisp::constant& cell )
{
	constant& cell_constant = cell;
	type_ref_ptr constant_type = nullptr;
	string number_string( cell_constant._unparsed_number.c_str() );
	if ( cell_constant._unevaled_type )
		constant_type = &context._type_evaluator( *cell_constant._unevaled_type );
	else
	{
		if ( number_string.find( '.' ) )
			constant_type = &context._type_library->get_type_ref( base_numeric_types::f64 );
		else
			constant_type = &context._type_library->get_type_ref( base_numeric_types::i64 );
	}
	uint8_t* num_value(nullptr);
	switch( context._type_library->to_base_numeric_type( *constant_type ) )
	{
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name )	\
	case base_numeric_types::name:	\
		num_value			\
			= parse_constant_value<numeric_type_to_c_type_map<base_numeric_types::name>::numeric_type>( context, number_string );	\
		break;
CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
	default:
		throw runtime_error( "Invalid constant type" );
	}
	if ( context._type_library->to_base_numeric_type( *constant_type ) 
		== base_numeric_types::no_known_type ) throw runtime_error( "invalid base numeric type" );
	return *context._ast_allocator->construct<numeric_constant_ast_node>( context._string_table, num_value, *constant_type );
}


ast_node& base_language_plugins::create_global_function_node( 
	slab_allocator_ptr alloc
	, const global_function_entry& entry
	, string_table_ptr st )
{
	return *alloc->construct<global_function_ast_node>( st, entry );
}


void base_language_plugins::register_base_compiler_plugins( string_table_ptr str_table
											, string_plugin_map_ptr top_level_special_forms
											, string_plugin_map_ptr special_forms
											, string_lisp_evaluator_map& lisp_evaluators )
{
	
	top_level_special_forms->insert( make_pair( str_table->register_str( "defn" )
		, make_shared<function_def_plugin>( str_table ) ) );
	top_level_special_forms->insert( make_pair( str_table->register_str( "defmacro" )
		, make_shared<macro_def_plugin>( str_table ) ) );
	top_level_special_forms->insert( make_pair( str_table->register_str( "def-pod" )
		, make_shared<pod_def_compiler_plugin>( str_table ) ) );

	compiler_plugin_ptr plugin = make_shared<template_fn_plugin>( str_table );
	top_level_special_forms->insert( make_pair( str_table->register_str( "define-template-fn" )
		, plugin ) );
	top_level_special_forms->insert( make_pair( str_table->register_str( "specialize-template-fn" )
		, plugin ) );

	top_level_special_forms->insert( make_pair( str_table->register_str( "def-macro-fn" )
		, make_shared<macro_function_plugin>( str_table ) ) );


	special_forms->insert( make_pair( str_table->register_str( "if" )
		, make_shared<if_compiler_plugin>( str_table ) ) );
	special_forms->insert( make_pair( str_table->register_str( "let" )
		, make_shared<let_compiler_plugin>( str_table ) ) );
	special_forms->insert( make_pair( str_table->register_str( "for" )
		, make_shared<for_loop_plugin>( str_table ) ) );
	special_forms->insert( make_pair( str_table->register_str( "set" )
		, make_shared<set_plugin>( str_table ) ) );
	special_forms->insert( make_pair( str_table->register_str( "get" )
		, make_shared<get_plugin>( str_table ) ) );
	special_forms->insert( make_pair( str_table->register_str( "numeric-cast" )
		, make_shared<numeric_cast_plugin>( str_table ) ) );
	special_forms->insert( make_pair( str_table->register_str( "ptr-cast" )
		, make_shared<ptr_cast_plugin>( str_table ) ) );

	generic_lisp_special_form::register_functions( str_table, lisp_evaluators );
	generic_preprocessor_function::register_functions( str_table, lisp_evaluators );

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


