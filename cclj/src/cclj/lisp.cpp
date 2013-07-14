//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/lisp.h"
#include "cclj/pool.h"

using namespace cclj;
using namespace cclj::lisp;


namespace
{
	class factory_impl : public factory
	{
		allocator_ptr			_allocator;
		pool<sizeof(type_ref)>	_object_pool;
		const cons_cell&		_empty_cell;
		vector<uint8_t*>		_buffer_allocs;

	public:
		factory_impl( allocator_ptr alloc, const cons_cell& empty_cell )
			: _allocator( alloc )
			, _object_pool( alloc, CCLJ_IMMEDIATE_FILE_INFO(), sizeof(void*) )
			, _empty_cell( empty_cell )
		{
		}
		~factory_impl()
		{
			for_each( _buffer_allocs.begin(), _buffer_allocs.end(),
				[this]( uint8_t* alloc )
			{
				_allocator->deallocate( alloc );
			} );
		}
		
		virtual const cons_cell& empty_cell() { return _empty_cell; }
		virtual cons_cell* create_cell() { return _object_pool.construct<cons_cell>(); }
		virtual array* create_array()  { return _object_pool.construct<array>(); }
		virtual symbol* create_symbol() { return _object_pool.construct<symbol>(); }
		virtual constant* create_constant() { return _object_pool.construct<constant>(); }
		virtual uint8_t* allocate_data( size_t size, uint8_t alignment )
		{
			if ( size <= sizeof(type_ref ) && alignment < sizeof(void*) )
			{
				return _object_pool.allocate();
			}
			else
			{
				uint8_t* retval = _allocator->allocate( size, alignment, CCLJ_IMMEDIATE_FILE_INFO() );
				_buffer_allocs.push_back( retval );
				return retval;
			}
		}
		virtual object_ptr_buffer allocate_obj_buffer(size_t size)
		{
			if ( size == 0 )
				return object_ptr_buffer();
			object_ptr* new_data = reinterpret_cast<object_ptr*>( 
				_allocator->allocate( size * sizeof( object_ptr* ), sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() ) );
			memset( new_data, 0, size * sizeof( object_ptr* ) );
			_buffer_allocs.push_back( reinterpret_cast<uint8_t*>( new_data ) );
			return object_ptr_buffer( new_data, size );
		}
	};

	struct type_map_key
	{
		string_table_str	_name;
		type_ref_ptr_buffer _specializations;
		size_t				_hash_code;
		type_map_key( string_table_str n, type_ref_ptr_buffer s )
			: _name( n )
			, _specializations( s )
		{
			_hash_code = std::hash<string_table_str>()( _name );
			for_each( _specializations.begin(), _specializations.end(), [this]
			( type_ref_ptr ref )
			{
				_hash_code = _hash_code ^ reinterpret_cast<size_t>( ref );
			} );
		}

		bool operator==( const type_map_key& other ) const
		{
			if ( _name == other._name )
			{
				if ( _specializations.size() == other._specializations.size() )
				{
					for ( size_t idx = 0, end = _specializations.size(); idx < end; ++idx )
					{
						if ( _specializations[idx] != other._specializations[idx] )
							return false;
					}
					return true;
				}
			}
			return false;
		}
	};
}

namespace std
{
	template<> struct hash<type_map_key>
	{
		size_t operator()( const type_map_key& k ) const { return k._hash_code; }
	};
}

namespace {

	class type_system_impl : public type_system
	{
		allocator_ptr		_allocator;
		string_table_ptr	_str_table;
		typedef unordered_map<type_map_key, type_ref_ptr> type_map;
		type_map _types;
	public:
		type_system_impl( allocator_ptr alloc, string_table_ptr str_t )
			: _allocator( alloc )
			, _str_table( str_t )
		{
		}

		~type_system_impl()
		{
			for_each( _types.begin(), _types.end(), 
			[this](type_map::value_type& type )
			{
				_allocator->deallocate( type.second );
			} );
		}
		
		virtual string_table_ptr string_table() { return _str_table; }

		virtual type_ref& get_type_ref( string_table_str name, type_ref_ptr_buffer _specializations )
		{
			type_map_key theKey( name, _specializations );
			type_map::iterator iter = _types.find( theKey );
			if ( iter != _types.end() ) return *iter->second;
			size_t type_size = sizeof( type_ref );
			size_t array_size = sizeof( type_ref_ptr ) * _specializations.size();
			uint8_t* mem = _allocator->allocate( type_size + array_size, sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() );
			type_ref* retval = reinterpret_cast<type_ref*>( mem );
			type_ref_ptr* array_data = reinterpret_cast<type_ref_ptr*>( mem + type_size );
			new (retval) type_ref();
			retval->_name = name;
			if ( array_size ) {
				memcpy( array_data, _specializations.begin(), array_size );
				retval->_specializations = type_ref_ptr_buffer( array_data, _specializations.size() );
			}
			theKey = type_map_key( name, retval->_specializations );
			_types.insert( make_pair( theKey, retval ) );
			return *retval;
		}
	};

	typedef unordered_map<string_table_str, object_ptr> macro_arg_map;
	typedef function<object_ptr (cons_cell* first_arg)> macro_function;
	struct preprocessor_op_types
	{
		enum _enum
		{
			no_op_type = 0,
			macro,
			poly_function,
		};
	};

	class preprocessor_op
	{
	public:
		virtual ~preprocessor_op(){}
		virtual preprocessor_op_types::_enum type() = 0;
	};
	typedef shared_ptr<preprocessor_op> preprocessor_op_ptr;
	typedef unordered_map<string_table_str, preprocessor_op_ptr > preprocessor_map;

	struct pp_op_traits
	{
		template<typename dtype>
		static dtype& cast_ref( shared_ptr<preprocessor_op> data )
		{
			if ( !data || data->type() != dtype::pp_op_type ) throw runtime_error( "Failed cast" );
			return *static_cast<dtype*>( data.get() );
		}
	};

	struct macro_def : public preprocessor_op
	{
		string_table_str					_name;
		object_ptr_buffer					_args;
		cons_cell*							_body;
		function<object_ptr (cons_cell*)>	_native_function;

		macro_def(string_table_str& s, object_ptr_buffer a, cons_cell& b )
			: _name( s )
			, _args( a )
			, _body( &b )
		{
		}

		macro_def(string_table_str& s, function<object_ptr (cons_cell*)> a )
			: _name( s )
			, _body( nullptr )
			, _native_function( a )
		{}

		macro_def() : _body( nullptr ) {}
		enum { pp_op_type = preprocessor_op_types::macro };
		virtual preprocessor_op_types::_enum type() { return preprocessor_op_types::macro; }
	};

	struct poly_function : public preprocessor_op
	{
		object_ptr_buffer	_type_args;
		symbol*				_name;
		object_ptr_buffer	_args;
		cons_cell*			_body;
		bool				_built_in;
		poly_function( array& ta, symbol& n, array& args, cons_cell& b )
			: _type_args( ta._data )
			, _name( &n )
			, _args( args._data )
			, _body( &b )
			, _built_in( false )
		{
		}
		poly_function() : _body( nullptr ), _built_in( false ) {}
		
		enum { pp_op_type = preprocessor_op_types::poly_function };
		virtual preprocessor_op_types::_enum type() { return preprocessor_op_types::poly_function; }
	};


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

	typedef function<void(cons_cell&)> preprocessor_form_function;
	typedef unordered_map<string_table_str, preprocessor_form_function> string_preprocessor_form_map;

	
	

	typedef unordered_map<string_table_str, poly_function> poly_function_map;
	typedef unordered_map<string_table_str, type_ref_ptr> symbol_type_ref_map;
	typedef unordered_set<string_table_str> string_set;
	typedef unordered_map<type_ref_ptr, type_ref_ptr> type_type_map;
	typedef function<type_ref_ptr(cons_cell&)> special_form_type_function;
	typedef unordered_map<string_table_str, special_form_type_function> string_special_form_map;

	struct pod_def
	{
		vector<symbol*> _fields;
	};
	typedef unordered_map<type_ref_ptr, pod_def> type_pod_map;

	//tool used during type checking.
#pragma warning(disable:4512)
	struct symbol_type_context : noncopyable
	{
		symbol_type_ref_map&							_context_symbol_types;
		vector<pair<string_table_str, type_ref_ptr> >	_created_symbols;
		symbol_type_context( symbol_type_ref_map& cst )
			: _context_symbol_types( cst )
		{
		}
		~symbol_type_context()
		{
			for_each( _created_symbols.begin(), _created_symbols.end(), [this]
			( const pair<string_table_str, type_ref_ptr>& symbol )
			{
				if ( symbol.second )
					_context_symbol_types[symbol.first] = symbol.second;
				else
					_context_symbol_types.erase( symbol.first );
			});
		}

		void add_symbol( string_table_str name, type_ref& type )
		{
			pair<symbol_type_ref_map::iterator,bool> inserter 
				= _context_symbol_types.insert( make_pair( name, &type ) );
			type_ref_ptr old_type = nullptr;
			if ( inserter.second == false )
			{
				//record the old type before overwriting
				old_type = inserter.first->second;
				inserter.first->second = &type;
			}
			//Record the old type to restore when this context is destroyed.
			_created_symbols.push_back( make_pair( name, old_type ) );
		}

	};

	class reader_impl : public reader
	{
		factory_ptr		_factory;
		type_system_ptr _type_system;
		string_table_ptr _str_table;
		const string* str;
		size_t cur_ptr;
		size_t end_ptr;
		vector<object_ptr> _top_objects;
		regex number_regex;

		string_preprocessor_form_map	_preprocessor_forms;

		preprocessor_map				_preprocessor_objects;
		symbol_type_ref_map				_context_symbol_types;
		string_set						_generated_function_names;
		//maps the function name+arg types to the return type.
		type_type_map					_function_definition_map;
		string_special_form_map			_special_form_type_processors;
		type_pod_map					_pod_definitions;

		string temp_str;
	public:
		reader_impl( factory_ptr f, type_system_ptr ts, string_table_ptr st ) 
			: _factory( f )
			, _type_system( ts )
			, _str_table( st )
			, number_regex( "[\\+-]?\\d+\\.?\\d*e?\\d*" ) 
		{
			register_preprocessor_form( bind( &reader_impl::defmacro_preprocessor_form, this, std::placeholders::_1 )
				, "defmacro" );
			register_preprocessor_form( bind( &reader_impl::def_poly_fn_preprocessor_form, this, std::placeholders::_1 )
				, "def-poly-fn" );

			//preexisting functions
			register_compiler_binary_float_fn( "+" );
			register_compiler_binary_float_fn( "-" );
			register_compiler_binary_float_fn( "*" );
			register_compiler_compare_float_fn( "<" );
			register_compiler_compare_float_fn( ">" );

			register_compiler_binary_int_fn( "+" );
			register_compiler_binary_int_fn( "-" );
			register_compiler_binary_int_fn( "*" );
			register_compiler_compare_int_unsigned_fn( "<" );
			register_compiler_compare_int_unsigned_fn( ">" );
			
			register_special_form( bind( &reader_impl::type_check_defn, this, std::placeholders::_1 ), "defn" );
			register_special_form( bind( &reader_impl::type_check_if, this, std::placeholders::_1 ), "if" );
			register_special_form( bind( &reader_impl::type_check_let, this, std::placeholders::_1 ), "let" );
			register_special_form( bind( &reader_impl::type_check_set, this, std::placeholders::_1 ), "set" );
			register_special_form( bind( &reader_impl::type_check_for, this, std::placeholders::_1 ), "for" );
			register_special_form( bind( &reader_impl::type_check_def_pod, this, std::placeholders::_1 ), "def-pod" );
			register_special_form( bind( &reader_impl::type_check_numeric_cast, this, std::placeholders::_1 ), "numeric-cast" );
		}


		void register_preprocessor_form( preprocessor_form_function fn, const char* name )
		{
			_preprocessor_forms.insert( make_pair( _str_table->register_str( name ), fn ) );
		}

		void register_special_form( special_form_type_function fn, const char* name )
		{
			_special_form_type_processors.insert( make_pair( _str_table->register_str( name ), fn ) );
		}
		
		void register_compiler_binary_fn( const char* name, const char* type )
		{
			type_ref& arg_type = _type_system->get_type_ref( type );
			type_ref* fn_args[2] = { &arg_type, &arg_type };
			type_ref& fn_type = _type_system->get_type_ref( _str_table->register_str( name )
										, type_ref_ptr_buffer( fn_args, 2 ) );
			_function_definition_map.insert( make_pair( &fn_type, &arg_type ) );
		}

		
		
		void register_compiler_binary_float_fn( const char* name )
		{
			register_compiler_binary_fn( name, "f32" );
			register_compiler_binary_fn( name, "f64" );
		}

		void register_compiler_binary_int_signed_fn( const char* name )
		{
			register_compiler_binary_fn( name, "i8" );
			register_compiler_binary_fn( name, "i16" );
			register_compiler_binary_fn( name, "i32" );
			register_compiler_binary_fn( name, "i64" );
		}
		
		void register_compiler_binary_int_unsigned_fn( const char* name )
		{
			register_compiler_binary_fn( name, "u8" );
			register_compiler_binary_fn( name, "u16" );
			register_compiler_binary_fn( name, "u32" );
			register_compiler_binary_fn( name, "u64" );
		}

		void register_compiler_binary_int_fn( const char* name )
		{
			register_compiler_binary_int_signed_fn( name );
			register_compiler_binary_int_unsigned_fn( name );
		}
		void register_compiler_compare_fn( const char* name, const char* type )
		{
			type_ref& arg_type = _type_system->get_type_ref( _str_table->register_str( type ) );
			type_ref& ret_type = _type_system->get_type_ref( _str_table->register_str( "i1" ) );
			type_ref* buffer[2] = { &arg_type, &arg_type};
			type_ref& func_type = _type_system->get_type_ref( _str_table->register_str( name)
											, type_ref_ptr_buffer( buffer, 2 ) );
			_function_definition_map.insert( make_pair( &func_type, &ret_type ) );
		}

		void register_compiler_compare_float_fn( const char* name )
		{
			register_compiler_compare_fn( name, "f32" );
			register_compiler_compare_fn( name, "f64" );
		}
		
		void register_compiler_compare_int_unsigned_fn( const char* name )
		{
			register_compiler_compare_fn( name, "u8" );
			register_compiler_compare_fn( name, "u16" );
			register_compiler_compare_fn( name, "u32" );
			register_compiler_compare_fn( name, "u64" );
		}
		
		void register_compiler_compare_int_signed_fn(const char* name )
		{
			register_compiler_compare_fn( name, "i8" );
			register_compiler_compare_fn( name, "i16" );
			register_compiler_compare_fn( name, "i32" );
			register_compiler_compare_fn( name, "i64" );
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
			if ( cur_ptr < end_ptr )
				return (*str)[cur_ptr];
			throw runtime_error( "str access out of bounds" );
		}
		void eatwhite()
		{
			while( cur_ptr != end_ptr && ( is_white( current_char() ) || current_char() == ';' ) )
			{
				if ( current_char() == ';' )
					for ( ; cur_ptr != end_ptr && current_char() != '\n'; ++cur_ptr ) {}
				else
					 for ( ; cur_ptr != end_ptr && is_white( current_char() ); ++cur_ptr ) {}
			}
		}

		void find_delimiter()
		{
			for ( ; cur_ptr != end_ptr && !is_delimiter( current_char() ); ++cur_ptr ) {}
		}

		bool atend()
		{
			return cur_ptr >= end_ptr || cur_ptr == string::npos;
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
			if ( start >= end_ptr || start == string::npos ) throw runtime_error( "fail" );
			if ( end > end_ptr ) end = end_ptr;
			temp_str = *str;
			temp_str.erase( temp_str.begin(), temp_str.begin() + start );
			temp_str.resize( end-start );
			return temp_str.c_str();
		}

		type_ref* parse_type()
		{
			size_t token_start = cur_ptr;
			find_delimiter();
			size_t token_end = cur_ptr;
			auto type_name = _str_table->register_str( substr( token_start, token_end ) );
			vector<type_ref_ptr> specializations;
			if ( current_char() == '[' )
				specializations = parse_type_array();
			return &_type_system->get_type_ref( type_name, specializations );
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
			regex_search( temp_str, m, number_regex );	

			//Parse each token.
			if ( m.empty() == false )
			{
				std::string number_string( temp_str );
				constant* new_constant = _factory->create_constant();
				//Define the type.  If the data is suffixed, then we have it.
				if( current_char() == '|' )
				{

					++cur_ptr;
					new_constant->_type = parse_type();
				}
				else 
				{
					if ( temp_str.find( '.' ) )
						new_constant->_type = &_type_system->get_type_ref( base_numeric_types::f64 );
					else
						new_constant->_type = &_type_system->get_type_ref( base_numeric_types::i64 );
				}
				switch( _type_system->to_base_numeric_type( *new_constant->_type ) )
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
				auto symbol_name = _str_table->register_str( temp_str.c_str() );

				type_ref* type_info = nullptr;
				if ( !atend() && current_char() == '|' )
				{
					++cur_ptr;
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
			size_t token_start = cur_ptr;
			size_t token_char = current_char();
			switch( token_char )
			{
			case '(': return parse_list();
			case '[': return parse_array();
				break;
			default:
				find_delimiter();
				return parse_number_or_symbol( token_start, cur_ptr );
			}
		}

		object_ptr parse_array() 
		{ 
			array* retval = _factory->create_array();
			++cur_ptr;
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
			++cur_ptr;

			if ( array_contents.size() )
			{
				retval->_data = _factory->allocate_obj_buffer( array_contents.size() );
				memcpy( retval->_data.begin(), &array_contents[0], array_contents.size() * sizeof( object_ptr ) );
			}
			return retval; 
		}

		vector<type_ref_ptr> parse_type_array()
		{
			++cur_ptr;
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
			++cur_ptr;

			return array_contents;
		}

		object_ptr parse_list()
		{
			if ( current_char() != '(' ) throw runtime_error( "fail" );
			++cur_ptr;
			eatwhite();
			if ( atend() ) throw runtime_error( "fail" );

			if (current_char() == ')' ) { ++cur_ptr; return const_cast<cons_cell*>( &_factory->empty_cell() ); };

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
			++cur_ptr;
			return retval;
		}

		object_ptr quote_section( cons_cell& item, macro_arg_map& arg_map )
		{
			//Quote takes one argument and returns that argument un-evaluated
			//unless there are unquotes in there.
			for ( cons_cell* top_item = &item; top_item; 
				top_item = object_traits::cast<cons_cell>( top_item->_next ) )
			{
				cons_cell* val = object_traits::cast<cons_cell>( top_item->_value );
				if( val )
				{
					symbol* val_name = object_traits::cast<symbol>( val->_value );
					if ( val_name && val_name->_name == _str_table->register_str( "unquote" ) )
					{
						cons_cell* next_cell = object_traits::cast<cons_cell>( val->_next );
						if ( next_cell == nullptr ) throw runtime_error( "bad unquote" );
						symbol* arg_name = object_traits::cast<symbol>( next_cell->_value );
						if ( arg_name == nullptr ) throw runtime_error( "bad unquote" );
						macro_arg_map::iterator iter = arg_map.find( arg_name->_name );
						if( iter == arg_map.end() ) throw runtime_error( "bad unquote" );
						top_item->_value = iter->second;
					}
					else
					{
						//nested things still get quoted
						quote_section( *val, arg_map );
					}
				}
			}

			return item._value;
		}

		object_ptr apply_macro_fn( cons_cell& fn_call, macro_arg_map& arg_map )
		{
			symbol* fn_name = object_traits::cast<symbol>( fn_call._value );
			if ( fn_name == nullptr ) throw runtime_error( "invalid macro" );

			
			if ( fn_name->_name == _str_table->register_str( "quote" ) )
			{
				//Quote takes one argument and returns that argument un-evaluated
				//unless there are unquotes in there.
				cons_cell* fn_data = object_traits::cast<cons_cell>( fn_call._next );
				if ( fn_data )
					return quote_section( *fn_data, arg_map );
				return _factory->create_cell();
			}
			else
			{
				preprocessor_map::iterator iter = _preprocessor_objects.find( fn_name->_name );
				if ( iter == _preprocessor_objects.end() ) throw runtime_error( "invalid preprocessor object" );
				macro_def& macro = pp_op_traits::cast_ref<macro_def>( iter->second );

				//eval the arguments here.
				cons_cell* arg_list = nullptr;
				cons_cell* fn_arg = object_traits::cast<cons_cell>( fn_call._next );
				if ( fn_arg )
				{
					arg_list = _factory->create_cell();
					cons_cell* list_item = nullptr;
					while( fn_arg )
					{
						if ( list_item == nullptr )
							list_item = arg_list;
						else
						{
							cons_cell* tmp = _factory->create_cell();
							list_item->_next = tmp;
							list_item = tmp;
						}
							
						object_ptr arg_value = eval_macro_arg( fn_arg->_value, arg_map );
						list_item->_value = arg_value;
					}
				}

				return apply_macro( macro, arg_list );
			}
		}

		object_ptr eval_macro_arg( object_ptr fn_arg, macro_arg_map& arg_map )
		{
			cons_cell* subcell = object_traits::cast<cons_cell>( fn_arg );
			if ( subcell )
			{
				return apply_macro_fn( *subcell, arg_map );
			}
			symbol* arg_name = object_traits::cast<symbol>( fn_arg );
			if ( arg_name )
			{
				macro_arg_map::iterator iter = arg_map.find( arg_name->_name );
				if ( iter != arg_map.end() )
				{
					return iter->second;
				}
			}
			return nullptr;
		}
		//Eventually we want to also pass in more context information so the macro
		//can be intelligent as to the types of data it is working with.
		object_ptr apply_macro( macro_def& macro, cons_cell* first_arg )
		{
			
			object_ptr retval = nullptr;
			if ( macro._body )
			{
				macro_arg_map arg_map;
				cons_cell* current_arg = first_arg;
				for_each( macro._args.begin(), macro._args.end(), [&]
				( object_ptr argname )
				{
					symbol& arg_name_symbol = object_traits::cast<symbol>( *argname );
					if ( current_arg )
					{
						arg_map[arg_name_symbol._name] = current_arg->_value;
						current_arg = object_traits::cast<cons_cell>( current_arg->_next );
					}
				} );
				for ( cons_cell* body_cell = macro._body; body_cell; 
					body_cell = object_traits::cast<cons_cell>( body_cell->_next ) )
				{
					if ( body_cell->_value == nullptr ) throw runtime_error( "invalid macro" );
					cons_cell& fn_call = object_traits::cast<cons_cell>( *body_cell->_value );
					retval = apply_macro_fn( fn_call, arg_map );
				}
			}
			else
			{
				retval = macro._native_function( first_arg );
			}
			return retval;
		}

		void write_type( type_ref& type, std::string& str )
		{
			str.append( type._name.c_str() );
			if ( type._specializations.size() )
			{
				str.append( "[" );
				bool first = true;
				for_each( type._specializations.begin(), type._specializations.end(), [&]
				( type_ref_ptr specialization )
				{
					if ( !first )
						str.append( " " );
					first = false;
					write_type( *specialization, str );
				} );
			}
		}

		type_ref_ptr rewrite_type( type_type_map& map, type_ref_ptr incoming_type )
		{
			type_type_map::iterator iter = map.find( incoming_type );
			if ( iter != map.end() )
				return iter->second;
			return incoming_type;
		}

		cons_cell& create_symbol_cell( string_table_str name, type_ref_ptr type = nullptr )
		{
			symbol* new_symbol = _factory->create_symbol();
			new_symbol->_name = name;
			new_symbol->_type = type;
			cons_cell* new_cell = _factory->create_cell();
			new_cell->_value = new_symbol;
			return *new_cell;
		}
		

		object_ptr apply_poly_fn( poly_function& fn, symbol& fn_call_site, cons_cell* first_arg )
		{
			//step 1 is to divine the type arguments.
			//1.  Use explicitly specified args.
			//2.  For any left over, divine if possible from actual call site arguments.
			//3.  make sure you found everything.
			type_type_map type_map;
			if ( fn_call_site._type )
			{
				unsigned idx = 0;
				for_each( fn_call_site._type->_specializations.begin(), fn_call_site._type->_specializations.end(), 
					[&]
				( type_ref* call_site_type )
				{
					if ( idx >= fn._type_args.size() ) throw runtime_error( "incorrect number of type arguments" );
					symbol& arg_name = object_traits::cast_ref<symbol>( fn._type_args[idx] );
					//It will be clear later why this is a map from type to type.
					type_ref& arg_name_type = _type_system->get_type_ref( arg_name._name );
					type_map.insert( make_pair( &arg_name_type, call_site_type ) );
					++idx;
				} );
			}

			//OK, now we can type the arguments
			unsigned arg_idx = 0;
			for ( cons_cell* cons_iter = first_arg; cons_iter; cons_iter = object_traits::cast<cons_cell>( cons_iter->_next ) )
			{
				if ( arg_idx >= fn._args.size() ) throw runtime_error( "poly function arg count mismatch" );
				symbol& def_arg = object_traits::cast_ref<symbol>( fn._args[arg_idx] );
				//Is this a generic argument type at all?
				bool is_generic = false;
				for( auto iter = fn._type_args.begin(), end = fn._type_args.end(); iter != end && is_generic == false;
					++ iter )
				{
					object_ptr type_arg = *iter;
					symbol& type_arg_sym = object_traits::cast_ref<symbol>( type_arg );
					type_ref& type_arg_type = _type_system->get_type_ref( type_arg_sym._name );
					is_generic = &type_arg_type == def_arg._type;
				}


				symbol& arg_sym = object_traits::cast_ref<symbol>( cons_iter->_value );
				symbol_type_ref_map::iterator iter = _context_symbol_types.find( arg_sym._name );
				if ( iter == _context_symbol_types.end() ) throw runtime_error( "unable to divine callsite argument type" );
				
				type_ref_ptr call_site_type = iter->second;

				if ( is_generic )
				{
					//If the type exists in the type map, ensure the explicitly provided type matches.
					type_type_map::iterator type_iter = type_map.find( def_arg._type );
					if ( type_iter != type_map.end() )
					{
						//ensure the provided type is the same as the other type.
						if ( type_iter->second != call_site_type ) 
							throw runtime_error( "types explicitly provided to polymorphic function do match types at call site" );
					}
					else
					{
						//we have provided a new type, map from provided abstract type variable to concrete type.
						type_map.insert( make_pair( def_arg._type, call_site_type ) );
					}
				}
				else
				{
					//Just normal type check
					if ( def_arg._type != call_site_type ) 
						throw runtime_error( "poly function call callsite type doesn't match definition type" );
				}
				++arg_idx;
			}
			if ( type_map.size() != fn._type_args.size() ) throw runtime_error( "polymorphic function failure" );
			
			//OK, we have divined the type of the polymorphic function.  Now we need to ensure we haven't output it
			//already.
			string generated_fn_name( fn._name->_name );
			generated_fn_name.append( "|[" );
			bool first = true;
			for_each( fn._type_args.begin(), fn._type_args.end(), [&]
			( object_ptr type_arg )
			{
				symbol& type_arg_sym = object_traits::cast_ref<symbol>( type_arg );
				type_ref_ptr type_var = &_type_system->get_type_ref( type_arg_sym._name );
				type_type_map::iterator iter = type_map.find( type_var );
				if ( iter == type_map.end() ) throw runtime_error( "Failed to handle type var" );
				type_ref_ptr concrete_type = iter->second;
				if ( !first )
					generated_fn_name.append( " " );
				first = false;
				write_type( *concrete_type, generated_fn_name );
			} );

			string_table_str gen_name = _str_table->register_str( generated_fn_name.c_str() );
			
			if ( _generated_function_names.find( gen_name ) == _generated_function_names.end() )
			{
				_generated_function_names.insert( gen_name );
				//check if this is a built-in polymorphic function


			}
			
			//re-write the function call to be the generated function name.
			symbol* new_call_site = _factory->create_symbol();
			new_call_site->_name = gen_name;
			new_call_site->_type = rewrite_type( type_map, fn._name->_type );
			cons_cell* new_call_site_list = _factory->create_cell();
			new_call_site_list->_value = new_call_site;
			new_call_site_list->_next = first_arg;
			return new_call_site_list;
		}

		type_ref_ptr lookup_symbol_type( symbol& s )
		{
			vector<string> parts = split_symbol( s );
			if ( parts.empty() ) throw runtime_error( "failed to figure out symbol type" );

			auto iter = _context_symbol_types.find( _str_table->register_str( parts[0].c_str() ) );
			if ( iter == _context_symbol_types.end() ) throw runtime_error( "unable to find symbol type" );
			type_ref_ptr current_type = iter->second;
			if ( parts.size() > 1 )
			{
				for ( size_t idx = 1, end = parts.size(); idx < end; ++idx )
				{
					type_pod_map::iterator pod_def_iter = _pod_definitions.find( iter->second );
					if ( pod_def_iter == _pod_definitions.end() ) throw runtime_error( "failed to find pod definition" );
					pod_def& current_def(pod_def_iter->second);
					auto table_name = _str_table->register_str( parts[idx].c_str() );
					vector<symbol*>::iterator pod_prop = find_if( current_def._fields.begin(), current_def._fields.end(),
					[&] ( symbol* prop )
					{
						return prop->_name == table_name;
					} );
					if ( pod_prop == current_def._fields.end() ) throw runtime_error( "unabled to find pod field" );
					current_type = (*pod_prop)->_type;
				}
			}
			return current_type;
		}

		//We have to use this sort of backhanded way because the parent object
		//could either be a cons cell *or* it could be an array.  Either way, a preprocessor
		//macro needs to be able to assign a completely new value to the parent.
		type_ref_ptr type_check_apply( cons_cell& first_cell, object_ptr& parent_expr_ptr )
		{
			symbol& item_name = object_traits::cast_ref<symbol>( first_cell._value );
			//this could be a special form, it could be a macro call or a poly function call.
			//check for macros, poly functions first.
			preprocessor_map::iterator pp_obj = _preprocessor_objects.find( item_name._name );
			if ( pp_obj != _preprocessor_objects.end() )
			{
				shared_ptr<preprocessor_op> item = pp_obj->second;
				cons_cell* next_cell = object_traits::cast<cons_cell>( first_cell._next );
				switch( item->type() )
				{
				case preprocessor_op_types::macro:
					parent_expr_ptr = apply_macro( pp_op_traits::cast_ref<macro_def>( item ), next_cell );
					break;
				case preprocessor_op_types::poly_function:
					parent_expr_ptr = apply_poly_fn( pp_op_traits::cast_ref<poly_function>( item ), item_name, next_cell );
					break;
				default:
					throw runtime_error( "failed to divine preprocessor obj type" );
				}
				return type_check_expr( parent_expr_ptr );
			}
			//next in line are the special forms
			string_special_form_map::iterator iter = _special_form_type_processors.find( item_name._name );
			if ( iter != _special_form_type_processors.end() )
				return iter->second( first_cell );

			//finally we assume this is a function call.  If it isn't or we can't find it, we are kind of SOL.
			//type check arguments.
			vector<type_ref_ptr> arg_map;
			for ( cons_cell* arg_cell = object_traits::cast<cons_cell>( first_cell._next );
				arg_cell; arg_cell = object_traits::cast<cons_cell>( arg_cell->_next ) )
			{
				type_ref_ptr expr_type = type_check_expr( arg_cell->_value );
				arg_map.push_back( expr_type );
			}
			type_ref& fn_type = _type_system->get_type_ref( item_name._name, arg_map );
			type_type_map::iterator fn_entry = _function_definition_map.find( &fn_type );
			if ( fn_entry == _function_definition_map.end() ) throw runtime_error( "unable to find function" );
			return fn_entry->second;
		}

		//This one is slightly different.  We need to pass in the cons cell that points
		//to this object in order for the re-writing to work.
		type_ref_ptr type_check_expr( object_ptr& expr_parent )
		{
			if ( expr_parent == nullptr ) throw runtime_error( "bad cell" );
			object_ptr expr = expr_parent;
			types::_enum cell_type = expr->type();
			switch( cell_type )
			{
			case types::cons_cell: return type_check_apply( object_traits::cast_ref<cons_cell>( expr ), expr_parent );
			case types::constant: 
				return object_traits::cast_ref<constant>( expr )._type;
			case types::symbol:
				return lookup_symbol_type( object_traits::cast_ref<symbol>( expr ) );
			default:
				throw runtime_error( "invalid expr type in type checker" );
			}
		}
		type_ref_ptr type_check_expr( cons_cell& expr_parent )
		{
			return type_check_expr( expr_parent._value );
		}

		type_ref_ptr type_check_numeric_cast( cons_cell& cell )
		{
			symbol& cast_name = object_traits::cast_ref<symbol>( cell._value );
			cons_cell& first_arg = object_traits::cast_ref<cons_cell>( cell._next );
			type_ref_ptr rettype = cast_name._type;
			type_ref_ptr first_arg_type = type_check_expr( first_arg._value );
			if ( !rettype ) throw runtime_error( "invalid numeric cast" );
			if ( first_arg._next ) throw runtime_error( "numeric cast only takes 1 argument" );
			base_numeric_types::_enum target_type = _type_system->to_base_numeric_type( *rettype );
			base_numeric_types::_enum source_type = _type_system->to_base_numeric_type( *first_arg_type );
			check_valid_numeric_cast_type( target_type );
			check_valid_numeric_cast_type( source_type );
			return rettype;
		}

		//we need to do type checking so that we can figure out
		type_ref_ptr type_check_defn( cons_cell& cell )
		{
			cons_cell& name_cell = object_traits::cast_ref<cons_cell>( cell._next );
			cons_cell& arg_cell = object_traits::cast_ref<cons_cell>( name_cell._next );
			cons_cell& body_cell = object_traits::cast_ref<cons_cell>( arg_cell._next );
			symbol& name = object_traits::cast_ref<symbol>( name_cell._value );
			if ( name._type == nullptr ) throw runtime_error( "invalid type of function dec, no return type" );
			object_ptr_buffer& args = object_traits::cast_ref<array>( arg_cell._value )._data;
			vector<type_ref_ptr> func_specs;
			symbol_type_context var_context( _context_symbol_types );
			for_each( args.begin(), args.end(), [&]
			( object_ptr arg )
			{
				symbol& arg_sym = object_traits::cast_ref<symbol>( arg );
				if ( arg_sym._type == nullptr ) 
					throw runtime_error( "Invalid type of function argument dec; no type provided" );
				func_specs.push_back( arg_sym._type );
				var_context.add_symbol( arg_sym._name, *arg_sym._type );
			} );
			type_ref& fn_type = _type_system->get_type_ref( name._name, func_specs );
			pair<type_type_map::iterator,bool> inserter = _function_definition_map.insert( make_pair( &fn_type, name._type ) );
			//here is a quandary.  Eventually we would like this to be more dynamic.
			if ( inserter.second == false ) throw runtime_error( "function x already defined" );
			type_ref_ptr retval = nullptr;
			for ( cons_cell* body_iter = &body_cell; body_iter; body_iter = object_traits::cast<cons_cell>( body_cell._next ) )
			{
				retval = type_check_expr( *body_iter );
			}
			return retval;
		}

		type_ref_ptr type_check_if( cons_cell& cell )
		{
			cons_cell& cond = object_traits::cast_ref<cons_cell>( cell._next );
			cons_cell& then = object_traits::cast_ref<cons_cell>( cond._next );
			cons_cell& els = object_traits::cast_ref<cons_cell>( then._next );
			type_ref_ptr cond_type = type_check_expr( cond );
			if ( base_numeric_types::i1 != _type_system->to_base_numeric_type( *cond_type ) )
				throw runtime_error( "invalid if condition" );
			type_ref_ptr then_type = type_check_expr( then );
			type_ref_ptr else_type = type_check_expr( els );
			if ( then_type != else_type ) throw runtime_error( "invalid if condition" );
			return then_type;
		}

		void enter_assign_block( object_ptr_buffer block_data, symbol_type_context& var_context )
		{
			if ( block_data.size() % 2 ) throw runtime_error( "invalid assign block" );
			for ( size_t idx = 0, end = block_data.size(); idx < end; idx += 2 )
			{
				symbol& item_name = object_traits::cast_ref<symbol>( block_data[idx] );
				type_ref_ptr expr_result = type_check_expr( block_data[idx+1] );
				var_context.add_symbol( item_name._name, *expr_result );
			}
		}

		type_ref_ptr type_check_let( cons_cell& cell )
		{
			symbol_type_context var_context( _context_symbol_types );
			cons_cell& assign_block = object_traits::cast_ref<cons_cell>( cell._next );
			cons_cell& let_body_start = object_traits::cast_ref<cons_cell>( assign_block._next );
			object_ptr_buffer assign_data = object_traits::cast_ref<array>( assign_block._value )._data;
			enter_assign_block( assign_data, var_context );
			type_ref_ptr retval = nullptr;
			for ( cons_cell* body_iter = &let_body_start; body_iter; 
				body_iter = object_traits::cast<cons_cell>( body_iter->_next ) )
			{
				retval = type_check_expr( *body_iter );
			}
			return retval;
		}
		type_ref_ptr type_check_set( cons_cell& cell )
		{
			cons_cell& name_cell = object_traits::cast_ref<cons_cell>( cell._next );
			cons_cell& expr_cell = object_traits::cast_ref<cons_cell>( name_cell._next );
			symbol& name = object_traits::cast_ref<symbol>( name_cell._value );
			type_ref_ptr expr = type_check_expr( expr_cell._value );
			type_ref_ptr existing_type = lookup_symbol_type( name );
			if ( expr != existing_type ) throw runtime_error( "Invalid set statement" );
			return existing_type;
		}
		type_ref_ptr type_check_for( cons_cell& cell )
		{
			cons_cell& entry_block = object_traits::cast_ref<cons_cell>( cell._next );
			object_ptr_buffer entry_block_data = object_traits::cast_ref<array>(entry_block._value)._data;
			cons_cell& cond_block = object_traits::cast_ref<cons_cell>( entry_block._next );
			cons_cell& update_block = object_traits::cast_ref<cons_cell>( cond_block._next );
			object_ptr_buffer update_block_data = object_traits::cast_ref<array>( update_block._value )._data;
			cons_cell& loop_body = object_traits::cast_ref<cons_cell>( update_block._next );

			symbol_type_context var_context( _context_symbol_types );
			enter_assign_block( entry_block_data, var_context );
			type_ref_ptr cond_type = type_check_expr( cond_block._value );
			if ( base_numeric_types::i1 != _type_system->to_base_numeric_type( *cond_type ) )
				throw runtime_error( "Invalid if statement, condition does not evaluate to boolean" );

			for ( cons_cell* body_iter = &loop_body; body_iter
				; body_iter = object_traits::cast<cons_cell>( body_iter->_next ) )
			{
				type_check_expr( body_iter->_value );
			}

			for_each( update_block_data.begin(), update_block_data.end(), [&]
			( object_ptr& item )
			{
				//return value ignored.
				type_check_expr( item );
			} );

			return &_type_system->get_type_ref( base_numeric_types::u32 );
		}

		type_ref_ptr type_check_def_pod( cons_cell& cell )
		{
			cons_cell& name_cell = object_traits::cast_ref<cons_cell>( cell._next );
			symbol& name = object_traits::cast_ref<symbol>( name_cell._value );
			type_ref& retval = _type_system->get_type_ref( name._name );
			auto inserter = _pod_definitions.insert( make_pair( &retval, pod_def() ) );
			if ( inserter.second == false ) throw runtime_error( "duplicate pod definition" );
			pod_def& new_def = inserter.first->second;
			vector<type_ref_ptr> constructor_args;
			for ( cons_cell* field_cell = object_traits::cast<cons_cell>( name_cell._next );
				field_cell; field_cell = object_traits::cast<cons_cell>( field_cell->_next ) )
			{
				symbol& name = object_traits::cast_ref<symbol>( field_cell->_value );
				new_def._fields.push_back( &name );
				constructor_args.push_back( name._type );
			}
			type_ref& cons_type = _type_system->get_type_ref( name._name, constructor_args );
			_function_definition_map.insert( make_pair( &cons_type, &retval ) );
			return &retval;
		}

		void defmacro_preprocessor_form( cons_cell& cell )
		{
			cons_cell& name_cell = object_traits::cast_ref<cons_cell>( cell._next );
			symbol& name_sym = object_traits::cast_ref<symbol>( name_cell._value );
			cons_cell& arg_cell = object_traits::cast_ref<cons_cell>( name_cell._next );
			array& arg_array = object_traits::cast_ref<array>( arg_cell._value );
			cons_cell& body_cell = object_traits::cast_ref<cons_cell>( arg_cell._next );
			_preprocessor_objects[name_sym._name] = make_shared<macro_def>( name_sym._name, arg_array._data, body_cell );
		}

		void def_poly_fn_preprocessor_form( cons_cell& cell )
		{
			cons_cell& type_array_cell = object_traits::cast_ref<cons_cell>( cell._next );
			array& type_array = object_traits::cast_ref<array>( type_array_cell._value );
			cons_cell& name_cell = object_traits::cast_ref<cons_cell>( type_array_cell._next );
			symbol& name = object_traits::cast_ref<symbol>( name_cell._value );
			cons_cell& arg_array_cell = object_traits::cast_ref<cons_cell>(name_cell._next);
			array& arg_array = object_traits::cast_ref<array>( arg_array_cell._value );
			cons_cell& body_cell = object_traits::cast_ref<cons_cell>( arg_array_cell._next );
			_preprocessor_objects[name._name] = make_shared<poly_function>( type_array, name
																				, arg_array, body_cell );
		}
		
		virtual object_ptr_buffer parse( const string& str )
		{
			cur_ptr = 0;
			end_ptr = str.size();
			_top_objects.clear();
			this->str = &str;
			while( atend() == false )
			{
				eatwhite();
				find_delimiter();
				if ( atend() == false )
				{
					if ( current_char() == '(' )
						_top_objects.push_back( parse_list() );
					else if ( current_char() == '[' )
						_top_objects.push_back( parse_array() );
				}
			}
			//expand macros.  This mean the lisp reader has a partial interpreter in it.
			for ( size_t idx = 0, end = _top_objects.size(); idx < end; ++idx )
			{
				cons_cell* item_cell = object_traits::cast<cons_cell>( _top_objects[idx] );
				if ( item_cell )
				{
					symbol* sym = object_traits::cast<symbol>( item_cell->_value );
					string_preprocessor_form_map::iterator iter = _preprocessor_forms.end();
					if ( sym )
						iter = _preprocessor_forms.find( sym->_name );
					if ( iter != _preprocessor_forms.end() )
					{
						iter->second( *item_cell );
						_top_objects.erase( _top_objects.begin() + idx );
						--idx;
						--end;
					}
					else //normal function definition or function call.  Simply traverse ignorantly 
					{
						type_check_expr( _top_objects[idx] );
					}
				}
			}
			return _top_objects;
		}
	};
}

factory_ptr factory::create_factory( allocator_ptr allocator, const cons_cell& empty_cell )
{
	return make_shared<factory_impl>( allocator, empty_cell );
}

type_system_ptr type_system::create_type_system( allocator_ptr allocator, string_table_ptr s )
{
	return make_shared<type_system_impl>( allocator, s );
}

reader_ptr reader::create_reader( factory_ptr factory, type_system_ptr ts, string_table_ptr str_table )
{
	return make_shared<reader_impl>( factory, ts, str_table );
}