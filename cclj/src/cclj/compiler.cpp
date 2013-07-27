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


	struct reader
	{
		string_table_ptr	_str_table;
		type_library_ptr	_type_library;
		factory_ptr			_factory;
		const string&		_str;
		string				_temp_str;
		size_t				_cur_ptr;
		size_t				_end_ptr;
		regex				_number_regex;

		reader( string_table_ptr st, type_library_ptr tl, factory_ptr f, const string& data )
			: _str_table( st )
			, _type_library( tl )
			, _factory( f )
			, _str( data )
			, _cur_ptr( 0 )
			, _end_ptr( data.size() )
			, _number_regex( "[\\+-]?\\d+\\.?\\d*e?\\d*" ) 
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

		type_ref* parse_type()
		{
			size_t token_start = _cur_ptr;
			find_delimiter();
			size_t token_end = _cur_ptr;
			auto type_name = _str_table->register_str( substr( token_start, token_end ) );
			vector<type_ref_ptr> specializations;
			if ( current_char() == '[' )
				specializations = parse_type_array();
			return &_type_library->get_type_ref( type_name, specializations );
		}

		template<typename number_type>
		uint8_t* parse_constant_value( const std::string& val )
		{
			number_type parse_val = str_to_num<number_type>::parse( val );
			uint8_t* retval = _factory->allocate_data( sizeof( number_type ), sizeof( number_type ) );
			memcpy( retval, &parse_val, sizeof( number_type ) );
			return retval;
		}

		
		object_ptr parse_number_or_symbol(size_t token_start, size_t token_end)
		{
			substr( token_start, token_end );
			smatch m;
			regex_search( _temp_str, m, _number_regex );	

			//Parse each token.
			if ( m.empty() == false )
			{
				std::string number_string( _temp_str );
				constant* new_constant = _factory->create_constant();
				//Define the type.  If the data is suffixed, then we have it.
				if( current_char() == '|' )
				{

					++_cur_ptr;
					new_constant->_type = parse_type();
				}
				else 
				{
					if ( _temp_str.find( '.' ) )
						new_constant->_type = &_type_library->get_type_ref( base_numeric_types::f64 );
					else
						new_constant->_type = &_type_library->get_type_ref( base_numeric_types::i64 );
				}
				switch( _type_library->to_base_numeric_type( *new_constant->_type ) )
				{
#define CCLJ_HANDLE_LIST_NUMERIC_TYPE( name )	\
				case base_numeric_types::name:	\
					new_constant->_value			\
						= parse_constant_value<numeric_type_to_c_type_map<base_numeric_types::name>::numeric_type>( number_string );	\
					break;
CCLJ_LIST_ITERATE_BASE_NUMERIC_TYPES
#undef CCLJ_HANDLE_LIST_NUMERIC_TYPE
				default:
					throw runtime_error( "Invalid constant type" );
				}
				return new_constant;
			}
			//symbol
			else
			{
				//symbols are far harder to parse.
				auto symbol_name = _str_table->register_str( _temp_str.c_str() );

				type_ref* type_info = nullptr;
				if ( !atend() && current_char() == '|' )
				{
					++_cur_ptr;
					type_info = parse_type();
				}
				symbol* retval = _factory->create_symbol();
				retval->_name = symbol_name;
				retval->_type = type_info;
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
			if ( current_char() == ']' )
				return retval;

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

		vector<type_ref_ptr> parse_type_array()
		{
			++_cur_ptr;
			vector<type_ref*> array_contents;
			eatwhite();
			if ( atend() ) throw runtime_error( "fail" );
			if ( current_char() == ']' )
				return vector<type_ref_ptr>();

			char test = current_char();
			while( current_char() != ']' )
			{
				array_contents.push_back( parse_type() );
				test = current_char();
			}
			++_cur_ptr;

			return array_contents;
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
							, type_ast_node_map_ptr top_level_symbols
							, slab_allocator_ptr ast_alloc )
							: _applier()
		{
			type_check_function tc = [this]( object_ptr cc ) -> ast_node&
			{ 
				return type_check_cell( cc ); 
			};
			_context = shared_ptr<reader_context>( new reader_context( alloc, f, l, st, tc, special_forms
											, top_level_special_forms, top_level_symbols
											, ast_alloc ) );
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
		type_ast_node_map_ptr			_top_level_symbols;
		slab_allocator_ptr				_ast_allocator;
		Module*							_module;
		shared_ptr<ExecutionEngine>		_exec_engine;
		shared_ptr<FunctionPassManager> _fpm;

		compiler_impl()
			: _allocator( allocator::create_checking_allocator() )
			, _str_table( string_table::create() )
			, _type_library( type_library::create_type_library( _allocator, _str_table ) )
			, _factory( factory::create_factory( _allocator, _empty_cell ) )
			, _special_forms( make_shared<string_plugin_map>() )
			, _top_level_special_forms( make_shared<string_plugin_map>() )
			, _top_level_symbols( make_shared<type_ast_node_map>() )
			, _ast_allocator( make_shared<slab_allocator<> >( _allocator ) )
			, _module( nullptr )
		{
			base_language_plugins::register_base_compiler_plugins( _str_table, _top_level_special_forms, _special_forms );
			register_function reg_fn = [this]( type_ref& fn_type, ast_node& comp_node )
			{
				_top_level_symbols->insert( make_pair( &fn_type, &comp_node ) );
			};
			binary_low_level_ast_node::register_binary_functions( reg_fn, _type_library, _str_table, _ast_allocator );
		}

		//transform text into the lisp datastructures.
		virtual vector<lisp::object_ptr> read( const string& text )
		{
			reader _reader( _str_table, _type_library, _factory, text );
			return _reader.read();
		}

		//Transform lisp datastructures into type-checked ast.
		virtual vector<ast_node_ptr> type_check( data_buffer<lisp::object_ptr> preprocess_result )
		{
			type_checker checker( _allocator, _factory, _type_library
								, _str_table, _special_forms
								, _top_level_special_forms, _top_level_symbols, _ast_allocator );

			vector<ast_node_ptr> type_check_results;
			for_each( preprocess_result.begin(), preprocess_result.end(), [&,this]
			( object_ptr pp_result )
			{
				if ( pp_result->type() == types::cons_cell )
				{
					cons_cell& top_cell = object_traits::cast_ref<cons_cell>( pp_result );
					symbol& first_item = object_traits::cast_ref<symbol>( top_cell._value );
					string_plugin_map::iterator iter = _top_level_special_forms->find( first_item._name );
					if ( iter != _top_level_special_forms->end() )
					{
						type_check_results.push_back( &iter->second->type_check( *checker._context, top_cell ) );
					}
					else
					{
						type_check_results.push_back( &checker._applier.type_check_apply( *checker._context, top_cell ) );
					}
				}
				else
					throw runtime_error( "invalid program, top level item is not a list" );
			} );
			return type_check_results;
		}

		//compile ast to binary.
		virtual pair<void*,type_ref_ptr> compile( data_buffer<ast_node_ptr> ast )
		{
			//run through and compile first steps.
			
			InitializeNativeTarget();
			LLVMContext &Context = getGlobalContext();
			if ( _module == nullptr )
			{
				_module = new Module("my cool jit", Context);

				// Create the JIT.  This takes ownership of the module.
				string ErrStr;
				_exec_engine = shared_ptr<ExecutionEngine> ( EngineBuilder(_module).setErrorStr(&ErrStr).create() );
				if (!_exec_engine) {
					throw runtime_error( "Could not create ExecutionEngine\n" );
				}
				_fpm = make_shared<FunctionPassManager>(_module);

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

			compiler_context comp_context( _type_library, _top_level_symbols, *_module, *_fpm );
			//The, ignoring all nodes that are not top level, create a function with the top level items
			//compile it, and return the results.
			for_each( ast.begin(), ast.end(), [&]
			( ast_node_ptr node )
			{
				node->compile_first_pass(comp_context);
			} );
			//compile each top level node, record the type of each non-top-level.
			type_ref_ptr rettype = nullptr;
			for_each( ast.begin(), ast.end(), [&]
			( ast_node_ptr node )
			{
				if ( node->executable_statement() == false )
					node->compile_second_pass( comp_context );
				else
					rettype = &node->type();
			} );

			if ( rettype == nullptr )
				return pair<void*,type_ref_ptr>( nullptr, nullptr );

			FunctionType* fn_type = FunctionType::get( comp_context.type_ref_type( *rettype ), false );
			Function* retfn = Function::Create( fn_type, GlobalValue::ExternalLinkage, "", _module );
			variable_context var_context( comp_context._variables );
			base_language_plugins::initialize_function( comp_context, *retfn, data_buffer<symbol*>(), var_context );
			
			llvm_value_ptr last_value = nullptr;
			for_each( ast.begin(), ast.end(), [&]
			( ast_node_ptr node )
			{
				if ( node->executable_statement() == true )
					last_value = node->compile_second_pass( comp_context ).first;
			} );
			if ( last_value == nullptr ) throw runtime_error( "unexpected compile result" );
			comp_context._builder.CreateRet( last_value );
			
			verifyFunction(*retfn );
			_fpm->run( *retfn );
			return make_pair( _exec_engine->getPointerToFunction( retfn ), rettype );
		}

		//Create a compiler and execute this text return the last value if it is a float else exception.
		virtual float execute( const string& text )
		{
			vector<object_ptr> read_result = read( text );
			vector<ast_node_ptr> type_check_result = type_check( read_result );
			pair<void*,type_ref_ptr> compile_result = compile( type_check_result );
			if ( _type_library->to_base_numeric_type( *compile_result.second ) != base_numeric_types::f32 )
				throw runtime_error( "failed to evaluate lisp data to float function" );
			typedef float (*anon_fn_type)();
			anon_fn_type exec_fn = reinterpret_cast<anon_fn_type>( compile_result.first );
			return exec_fn();
		}
	}; 
}

compiler_ptr compiler::create()
{
	return make_shared<compiler_impl>();
}