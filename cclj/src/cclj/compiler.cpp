//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/compiler.h"
#include "cclj/plugins/base_plugins.h"
#include "cclj/plugins/preprocessor_plugins.h"
#include "cclj/module.h"
extern "C"
{
#include "pcre.h"
}
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


namespace {


	struct pcre_simple_regex
	{
		pcre* _re;
		const char* _errorMsg;
		int _errOffset;
		pcre_simple_regex( const char* pattern )
		{
			_re = pcre_compile( pattern, 0, &_errorMsg, &_errOffset, nullptr );
			if (!_re )
				throw runtime_error( "invalid regex" );
		}
		~pcre_simple_regex()
		{
			pcre_free( _re );
		}

		bool match( const char* str )
		{
			if ( str == nullptr ) return false;
			int rc = pcre_exec( _re, nullptr, str, strlen(str), 0, 0, nullptr, 0 );
			return rc >= 0;
		}

	};

	struct reader
	{
		string_table_ptr	_str_table;
		type_library_ptr	_type_library;
		factory_ptr			_factory;
		const string&		_str;
		string				_temp_str;
		size_t				_cur_ptr;
		size_t				_end_ptr;
		pcre_simple_regex	_number_regex;

		reader( string_table_ptr st, type_library_ptr tl, factory_ptr f, const string& data )
			: _str_table( st )
			, _type_library( tl )
			, _factory( f )
			, _str( data )
			, _cur_ptr( 0 )
			, _end_ptr( data.size() )
			, _number_regex( "^[\\+-]?\\d+\\.?\\d*e?\\d*" ) 
		{
		}
		

		static bool is_white(char data)
		{
			return data == ' '
					|| data == '\t'
					|| data == '\n'
					|| data == '\r';
		}
		static bool is_delimiter(char data )
		{
			return is_white( data )
				|| data == '|'
				|| data == '['
				|| data == '('
				|| data == ']'
				|| data == ')'
				|| data == ';';
		}
		char current_char()
		{
			if ( _cur_ptr < _end_ptr )
				return _str[_cur_ptr];
			throw runtime_error( "str access out of bounds" );
		}
		void eatwhite()
		{
			while( _cur_ptr != _end_ptr && ( is_white( current_char() ) || current_char() == ';' ) )
			{
				if ( current_char() == ';' )
					for ( ; _cur_ptr != _end_ptr && current_char() != '\n'; ++_cur_ptr ) {}
				else
					 for ( ; _cur_ptr != _end_ptr && is_white( current_char() ); ++_cur_ptr ) {}
			}
		}

		void find_delimiter()
		{
			for ( ; _cur_ptr != _end_ptr && !is_delimiter( current_char() ); ++_cur_ptr ) {}
		}

		bool atend()
		{
			return _cur_ptr >= _end_ptr || _cur_ptr == string::npos;
		}

		bool isnum()
		{
			return current_char() >= '0' && current_char() <= '9';
		}

		bool isplusminus()
		{
			return current_char() == '-' || current_char() == '+';
		}

		const char* substr( size_t start, size_t end )
		{
			if ( start >= _end_ptr || start == string::npos ) throw runtime_error( "fail" );
			if ( end > _end_ptr ) end = _end_ptr;
			_temp_str = _str;
			_temp_str.erase( _temp_str.begin(), _temp_str.begin() + start );
			_temp_str.resize( end-start );
			return _temp_str.c_str();
		}

		cons_cell* parse_type()
		{
			object_ptr item = parse_next_item();
			if ( item->type() == types::symbol )
			{
				cons_cell* retval = _factory->create_cell();
				retval->_value = item;
				if ( current_char() == '[' )
				{
					cons_cell* next_cell = _factory->create_cell();
					retval->_next = next_cell;
					next_cell->_value = parse_next_item();
				}
				return retval;
			}
			else if ( item->type() == types::array )
			{
				cons_cell* retval = _factory->create_cell();
				cons_cell* next_cell = _factory->create_cell();
				retval->_next = next_cell;
				next_cell->_value = item;
				return retval;
			}
			return &object_traits::cast_ref<cons_cell>( item );
		}


		
		object_ptr parse_number_or_symbol(size_t token_start, size_t token_end)
		{
			substr( token_start, token_end );
			bool is_number = _number_regex.match( _temp_str.c_str() );

			//Parse each token.
			if ( is_number )
			{
				string number_string( _temp_str );
				constant* new_constant = _factory->create_constant();
				new_constant->_unparsed_number = _str_table->register_str( number_string.c_str() );
				//Define the type.  If the data is suffixed, then we have it.
				if( current_char() == '|' )
				{
					++_cur_ptr;
					new_constant->_unevaled_type = parse_type();
				}
				else
				{
					new_constant->_unevaled_type = _factory->create_cell();
					symbol* type_name = _factory->create_symbol();
					if ( number_string.find( "." ) != string::npos )
						type_name->_name = _str_table->register_str( "f64" );
					else
						type_name->_name = _str_table->register_str( "i64" );
					new_constant->_unevaled_type->_value = type_name;
				}

				return new_constant;
			}
			//symbol
			else
			{
				//symbols are far harder to parse.
				auto symbol_name = _str_table->register_str( _temp_str.c_str() );

				cons_cell* type_info = nullptr;
				if ( !atend() && current_char() == '|' )
				{
					++_cur_ptr;
					type_info = parse_type();
				}
				symbol* retval = _factory->create_symbol();
				retval->_name = symbol_name;
				retval->_unevaled_type = type_info;
				return retval;
			}

		}

		object_ptr parse_next_item()
		{
			eatwhite();
			size_t token_start = _cur_ptr;
			size_t token_char = current_char();
			switch( token_char )
			{
			case '(': return parse_list();
			case '[': return parse_array();
				break;
			default:
				find_delimiter();
				return parse_number_or_symbol( token_start, _cur_ptr );
			}
		}

		object_ptr parse_array() 
		{ 
			array* retval = _factory->create_array();
			++_cur_ptr;
			if ( atend() ) throw runtime_error( "fail" );
			if ( current_char() == ']' ) {
				++_cur_ptr;
				return retval;
			}

			vector<object_ptr> array_contents;
			eatwhite();

			while( current_char() != ']' )
			{
				array_contents.push_back( parse_next_item() );
				eatwhite();
			}
			++_cur_ptr;

			if ( array_contents.size() )
			{
				retval->_data = _factory->allocate_obj_buffer( array_contents.size() );
				memcpy( retval->_data.begin(), &array_contents[0], array_contents.size() * sizeof( object_ptr ) );
			}
			return retval; 
		}

		object_ptr parse_list()
		{
			if ( current_char() != '(' ) throw runtime_error( "fail" );
			++_cur_ptr;
			eatwhite();
			if ( atend() ) throw runtime_error( "fail" );

			if (current_char() == ')' ) { ++_cur_ptr; return const_cast<cons_cell*>( &_factory->empty_cell() ); };

			if ( current_char() == '(' ) throw runtime_error( "nested lists; invalid parsing" );
			
			cons_cell* retval = _factory->create_cell();
			cons_cell* next_cell = nullptr;
			while( current_char() != ')' )
			{
				if ( next_cell == nullptr )
					next_cell = retval;
				else
				{
					auto temp = _factory->create_cell();
					next_cell->_next = temp;
					next_cell = temp;
				}
				next_cell->_value = parse_next_item();
				eatwhite();
			}
			//inc past the )
			++_cur_ptr;
			return retval;
		}
		vector<object_ptr> read()
		{
			_cur_ptr = 0;
			_end_ptr = _str.size();
			vector<object_ptr> retval;
			while( atend() == false )
			{
				eatwhite();
				find_delimiter();
				if ( atend() == false )
				{
					if ( current_char() == '(' )
						retval.push_back( parse_list() );
					else if ( current_char() == '[' )
						retval.push_back( parse_array() );
				}
			}
			return retval;
		}
	};

	struct type_checker
	{
		shared_ptr<reader_context>				_context;
		base_language_plugins					_applier;
		type_checker( allocator_ptr alloc, lisp::factory_ptr f, type_library_ptr l
							, string_table_ptr st
							, string_plugin_map_ptr special_forms
							, string_plugin_map_ptr top_level_special_forms
							, slab_allocator_ptr ast_alloc
							, string_lisp_evaluator_map& lisp_evals
							, qualified_name_table_ptr name_table
							, module_ptr module)
							: _applier()
		{
			type_check_function tc = [this]( object_ptr cc ) -> ast_node&
			{ 
				return type_check_cell( cc ); 
			};
			type_eval_function te = [this](cons_cell& c) -> type_ref&
			{
				return eval_cell_to_type( c );
			};
			_context = shared_ptr<reader_context>( new reader_context( alloc, f, l, st, tc, te, special_forms
											, top_level_special_forms
											, ast_alloc, lisp_evals, name_table, module));
		}

		ast_node& type_check_cell( object_ptr value )
		{
			if ( value == nullptr ) throw runtime_error( "Invalid type check value" );

			switch( value->type() )
			{
			case types::symbol:
				return _applier.type_check_symbol( *_context, object_traits::cast_ref<symbol>( value ) );
			case types::cons_cell:
				return _applier.type_check_apply( *_context, object_traits::cast_ref<cons_cell>( value ) );
			case types::constant:
				return _applier.type_check_numeric_constant( *_context, object_traits::cast_ref<constant>( value ) );
			default:
				throw runtime_error( "unable to type check at this time" );
			}
		}

		type_ref& eval_symbol_array_to_type( symbol& name, array* vals )
		{
			vector<type_ref_ptr> val_types;
			if ( vals )
			{
				for ( size_t idx = 0, end = vals->_data.size(); idx < end; ++idx )
				{
					symbol& sub_type = object_traits::cast_ref<symbol>( vals->_data[idx] );
					array* sub_specializations( nullptr );
					if ( idx < end - 1 )
						sub_specializations = object_traits::cast<array>( vals->_data[idx+1] );
					if ( sub_specializations )
						++idx;
					val_types.push_back( &eval_symbol_array_to_type( sub_type, sub_specializations ) );
				}
			}
			return _context->_type_library->get_type_ref( name._name, val_types );
		}

		type_ref& eval_cell_to_type( cons_cell& cell )
		{
			//One of two cases.  Either the cell points to a type object
			//or we can evaluate it as a type directly.
			if ( cell._value == nullptr ) throw runtime_error( "invalid cell value" );
			if ( cell._value->type() == types::symbol )
			{
				symbol& type_name = object_traits::cast_ref<symbol>( cell._value );
				array* specs( nullptr );
				if ( cell._next )
				{
					cons_cell& spec_cell = object_traits::cast_ref<cons_cell>( cell._next );
					specs = &object_traits::cast_ref<array>( spec_cell._value );
				}
				return eval_symbol_array_to_type( type_name, specs );
			}
			else
				throw runtime_error( "invalid type in cell" );
		}
	};

	struct global_variable_entry
	{
		void*				_value;
		string_table_str	_name;
		type_ref_ptr		_type;

		global_variable_entry()
			: _value( nullptr )
			, _type( nullptr )
		{
		}
		
		global_variable_entry(void* v, string_table_str s, type_ref& t )
			: _value( v )
			, _name( s )
			, _type( &t )
		{
		}
	};


	struct compiler_impl : public compiler
	{
		allocator_ptr					_allocator;
		string_table_ptr				_str_table;
		type_library_ptr				_type_library;
		factory_ptr						_factory;
		cons_cell						_empty_cell;
		string_plugin_map_ptr			_special_forms;
		string_plugin_map_ptr			_top_level_special_forms;
		slab_allocator_ptr				_ast_allocator;
		Module*							_llvm_module;
		shared_ptr<ExecutionEngine>		_exec_engine;
		shared_ptr<FunctionPassManager> _fpm;
		string_lisp_evaluator_map		_evaluators;
		qualified_name_table_ptr		_name_table;
		module_ptr						_module;

		compiler_impl()
			: _allocator( allocator::create_checking_allocator() )
			, _str_table( string_table::create() )
			, _type_library( type_library::create_type_library( _allocator, _str_table ) )
			, _factory( factory::create_factory( _allocator, _empty_cell ) )
			, _special_forms( make_shared<string_plugin_map>() )
			, _top_level_special_forms( make_shared<string_plugin_map>() )
			, _ast_allocator( make_shared<slab_allocator<> >( _allocator ) )
			, _llvm_module( nullptr )
			, _name_table(qualified_name_table::create_table(_str_table))
			, _module(module::create_module(_str_table, _type_library, _name_table))
		{
			base_language_plugins::register_base_compiler_plugins( _str_table, _top_level_special_forms, _special_forms, _evaluators );
			preprocessor_plugins::register_plugins(_name_table, _top_level_special_forms, _special_forms, _evaluators);
			binary_low_level_ast_node::register_binary_functions( _module, _type_library, _name_table );
			type_ref& base_type = _type_library->get_type_ref( base_numeric_types::i32 );
			type_ref& ptr_lvl1 = _type_library->get_ptr_type( base_type );
			type_ref& runtime_type = ptr_lvl1;
			variable_node_factory& rt_variable = _module->define_variable(_name_table->register_name("rt"), ptr_lvl1);
			rt_variable.set_value(this);

			{
				type_ref& ret_type = _type_library->get_ptr_type( base_numeric_types::u8 );
				type_ref* arg_types[3] = {
					&runtime_type
					, &_type_library->get_type_ref( base_numeric_types::u32 )
					, &_type_library->get_type_ref( base_numeric_types::u8 )
				};

				function_factory& fn = _module->define_function(_name_table->register_name("malloc"), type_ref_ptr_buffer(arg_types,3), ret_type);
				fn.set_function_body(reinterpret_cast<void*>(&compiler_impl::rt_malloc));
			}
			{
				type_ref& ret_type = _type_library->get_void_type();
				type_ref* arg_types[2] = { &runtime_type, &_type_library->get_unqual_ptr_type() };
				function_factory& fn = _module->define_function(_name_table->register_name("free"), type_ref_ptr_buffer(arg_types, 2), ret_type);
				fn.set_function_body(reinterpret_cast<void*>(&compiler_impl::rt_free));
			}
		}

		virtual module_ptr module() { return _module; }

		//transform text into the lisp datastructures.
		virtual vector<lisp::object_ptr> read( const string& text )
		{
			reader _reader( _str_table, _type_library, _factory, text );
			return _reader.read();
		}

		//Transform lisp datastructures into type-checked ast.
		virtual void type_check( data_buffer<lisp::object_ptr> preprocess_result )
		{
			type_checker checker( _allocator, _factory, _type_library
								, _str_table, _special_forms
								, _top_level_special_forms, _ast_allocator
								, _evaluators, _name_table, _module );

			for_each( preprocess_result.begin(), preprocess_result.end(), [&,this]
			( object_ptr pp_result )
			{
				if ( pp_result->type() == types::cons_cell )
				{
					cons_cell& top_cell = object_traits::cast_ref<cons_cell>( pp_result );
					symbol& first_item = object_traits::cast_ref<symbol>( top_cell._value );
					string_plugin_map::iterator iter = _top_level_special_forms->find( first_item._name );
					ast_node_ptr typecheck_result = nullptr;
					if ( iter != _top_level_special_forms->end() )
					{
						typecheck_result = iter->second->type_check(*checker._context, top_cell);
					}
					else
					{
						typecheck_result = &checker._applier.type_check_apply(*checker._context, top_cell);
					}
					if (typecheck_result)
						_module->append_init_ast_node(*typecheck_result);
				}
				else
					throw runtime_error( "invalid program, top level item is not a list" );
			} );
		}

		//compile ast to binary.
		virtual pair<void*,type_ref_ptr> compile()
		{
			//run through and compile first steps.
			
			InitializeNativeTarget();
			LLVMContext &Context = getGlobalContext();
			if (_llvm_module == nullptr)
			{
				_llvm_module = new Module("my cool jit", Context);

				// Create the JIT.  This takes ownership of the module.
				string ErrStr;
				_exec_engine = shared_ptr<ExecutionEngine>(EngineBuilder(_llvm_module).setErrorStr(&ErrStr).create());
				if (!_exec_engine) {
					throw runtime_error( "Could not create ExecutionEngine\n" );
				}
				_fpm = make_shared<FunctionPassManager>(_llvm_module);

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
			}

			compiler_context comp_context(_type_library, _name_table, _module, *_llvm_module, *_fpm, *_exec_engine);

			_module->compile_first_pass(comp_context);
			_module->compile_second_pass(comp_context);

			return make_pair(_exec_engine->getPointerToFunction(&_module->llvm()), &_module->init_return_type());
		}

		//Create a compiler and execute this text return the last value if it is a float else exception.
		virtual float execute( const string& text )
		{
			vector<object_ptr> read_result = read( text );
			type_check( read_result );
			pair<void*,type_ref_ptr> compile_result = compile();
			if ( _type_library->to_base_numeric_type( *compile_result.second ) != base_numeric_types::f32 )
				throw runtime_error( "failed to evaluate lisp data to float function" );
			typedef float (*anon_fn_type)();
			anon_fn_type exec_fn = reinterpret_cast<anon_fn_type>( compile_result.first );
			return exec_fn();
		}

		static void* rt_malloc( void* comp_ptr, uint32_t item_size, uint8_t item_align )
		{
			compiler_impl* compiler = reinterpret_cast<compiler_impl*>( comp_ptr );
			return compiler->_allocator->allocate( item_size, item_align, CCLJ_IMMEDIATE_FILE_INFO() );
		}

		static void rt_free( void* comp_ptr, void* value )
		{
			compiler_impl* compiler = reinterpret_cast<compiler_impl*>( comp_ptr );
			compiler->_allocator->deallocate( value );
		}
	}; 
}

compiler_ptr compiler::create()
{
	return make_shared<compiler_impl>();
}
