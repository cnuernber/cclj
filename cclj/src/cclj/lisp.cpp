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
		vector<object_ptr*>		_buffer_allocs;

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
				[this]( object_ptr* alloc )
			{
				_allocator->deallocate( alloc );
			} );
		}
		
		virtual const cons_cell& empty_cell() { return _empty_cell; }
		virtual cons_cell* create_cell() { return _object_pool.construct<cons_cell>(); }
		virtual array* create_array()  { return _object_pool.construct<array>(); }
		virtual symbol* create_symbol() { return _object_pool.construct<symbol>(); }
		virtual constant* create_constant() { return _object_pool.construct<constant>(); }
		virtual type_ref* create_type_ref() { return _object_pool.construct<type_ref>(); }
		virtual object_ptr_buffer allocate_obj_buffer(size_t size)
		{
			if ( size == 0 )
				return object_ptr_buffer();
			object_ptr* new_data = reinterpret_cast<object_ptr*>( 
				_allocator->allocate( size * sizeof( object_ptr* ), sizeof(void*), CCLJ_IMMEDIATE_FILE_INFO() ) );
			memset( new_data, 0, size * sizeof( object_ptr* ) );
			_buffer_allocs.push_back( new_data );
			return object_ptr_buffer( new_data, size );
		}
	};

	class reader_impl : public reader
	{
		factory_ptr _factory;
		string_table_ptr _str_table;
		const string* str;
		size_t cur_ptr;
		size_t end_ptr;
		vector<object_ptr> _top_objects;
		regex number_regex;

		string temp_str;
	public:
		reader_impl( factory_ptr f, string_table_ptr st ) 
			: _factory( f )
			, _str_table( st )
			, number_regex( "[\\+-]?\\d+\\.?\\d*e?\\d*" ) {}

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
			type_ref* type_info = nullptr;
			size_t token_start = cur_ptr;
			find_delimiter();
			size_t token_end = cur_ptr;
			type_info = _factory->create_type_ref();
			type_info->_name = _str_table->register_str( substr( token_start, token_end ) );

			if ( !atend() && current_char() == '[' )
				type_info->_specializations = parse_type_array();

			return type_info;
		}

		object_ptr parse_number_or_symbol(size_t token_start, size_t token_end)
		{
			substr( token_start, token_end );
			smatch m;
			regex_search( temp_str, m, number_regex );
						

			//Parse each token.
			if ( m.empty() == false )
			{
				constant* new_constant = _factory->create_constant();
				new_constant->value = static_cast<float>(atof( temp_str.c_str() ) );
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

		object_ptr_buffer parse_type_array()
		{
			object_ptr_buffer  retval;
			vector<object_ptr> array_contents;
			eatwhite();
			if ( atend() ) throw runtime_error( "fail" );
			if ( current_char() == ']' )
				return retval;

			while( current_char() != ']' )
			{
				array_contents.push_back( parse_type() );
			}
			++cur_ptr;

			if ( array_contents.size() )
			{
				retval = _factory->allocate_obj_buffer( array_contents.size() );
				memcpy( retval.begin(), &array_contents[0], array_contents.size() * sizeof( object_ptr ) );
			}
			return retval; 
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
			return _top_objects;
		}
	};
}

factory_ptr factory::create_factory( allocator_ptr allocator, const cons_cell& empty_cell )
{
	return make_shared<factory_impl>( allocator, empty_cell );
}

reader_ptr reader::create_reader( factory_ptr factory, string_table_ptr str_table )
{
	return make_shared<reader_impl>( factory, str_table );
}