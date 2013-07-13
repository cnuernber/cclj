//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/cclj.h"
#include "cclj/state.h"
#define NOMINMAX
#include <windows.h>

using namespace cclj;

namespace 
{

string executable_path()
{
#ifdef _WIN32
	char temp_buf[1024];
	GetModuleFileNameA ( NULL, temp_buf, 1024 );
	return temp_buf;
#endif
}

path corpus_dir()
{
	path exec_path( executable_path() );
	path dir = exec_path.parent_path();
	while( !dir.empty() )
	{
		path test = dir;
		test /= "corpus";
		if ( is_directory( test ) )
			return test;
		dir = dir.parent_path();
	}
	throw runtime_error( "Failed to find test dir" );
}

path corpus_file( const char* fname )
{
	auto test_file = corpus_dir();
	test_file /= fname;
	if ( !is_regular( test_file ) )
		throw runtime_error( "failed to find test file" );
	return test_file;
}

string corpus_file_text( const char* fname )
{
	auto filename = corpus_file( fname );
	ifstream input;
	input.open( filename, std::ios_base::in );
	char temp_buf[2056];
	string retval;

	do
	{
		input.read( temp_buf, 2056 );
		auto read_amount = input.gcount();
		if ( read_amount )
			retval.append( temp_buf, static_cast<size_t>( read_amount ) );

	} while (input.eof() == false );

	return retval;
}

}


#define DEFINE_SIMPLE_CORPUS_TEST( name, answer )			\
TEST(corpus_tests, ##name )									\
{															\
	auto test_data = corpus_file_text( #name ".cclj" );		\
	auto state_ptr = state::create_state();					\
	float test_result = state_ptr->execute( test_data );	\
	ASSERT_EQ( answer, test_result );						\
}

DEFINE_SIMPLE_CORPUS_TEST( basic1, 3.0f );
DEFINE_SIMPLE_CORPUS_TEST( basic2, 8.0f );
DEFINE_SIMPLE_CORPUS_TEST( basic3, 20.0f );
DEFINE_SIMPLE_CORPUS_TEST( basic4, -100.0f );
DEFINE_SIMPLE_CORPUS_TEST( basic_struct, 15.0f );
DEFINE_SIMPLE_CORPUS_TEST( for_loop, 125.0f );
//DEFINE_SIMPLE_CORPUS_TEST( poly_fn, 20.0f );


TEST(regex_tests, symbol_regex)
{
	regex symbol_regex( "[\\+-]?\\d+\\.?\\d*e?\\d*" );
	smatch match;
	regex_search( string( "+51" ), match, symbol_regex );
	regex_search( string( "-51" ), match, symbol_regex );
	regex_search( string( "+51.54" ), match, symbol_regex );
	regex_search( string( "-51.54" ), match, symbol_regex );
	regex_search( string( "51e10" ), match, symbol_regex );
	regex_search( string( "51.54e10" ), match, symbol_regex );
}