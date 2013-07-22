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

using namespace cclj;
using namespace cclj::lisp;
using namespace cclj::plugins;


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

	struct compiler_impl : public compiler
	{
		allocator_ptr		_allocator;
		string_table_ptr	_str_table;
		type_library_ptr	_type_library;
		factory_ptr			_factory;
		cons_cell			_empty_cell;


		compiler_impl()
			: _allocator( allocator::create_checking_allocator() )
			, _str_table( string_table::create() )
			, _type_library( type_library::create_type_library( _allocator, _str_table ) )
			, _factory( factory::create_factory( _allocator, _empty_cell ) )
		{

		}

		//transform text into the lisp datastructures.
		virtual vector<lisp::object_ptr> read( const string& text )
		{
			reader _reader( _str_table, _type_library, _factory, text );
			return _reader.read();
		}

		//Preprocess evaluates macros and fills in polymorphic functions and types.
		virtual vector<lisp::object_ptr> preprocess( data_buffer<lisp::object_ptr> read_result ) = 0;

		//Transform lisp datastructures into type-checked ast.
		virtual vector<ast_node_ptr> type_check( data_buffer<lisp::object_ptr> preprocess_result ) = 0;

		//compile ast to binary.
		virtual pair<void*,type_ref_ptr> compile( data_buffer<ast_node_ptr> ) = 0;

		//Create a compiler and execute this text return the last value if it is a float else exception.
		virtual float execute( const string& text ) = 0;
	};
}