//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/cclj.h"
#include "cclj/compiler.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

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

bool is_directory(const string& str )
{
#ifdef _WIN32
	DWORD atts = GetFileAttributesA( str.c_str() );
	return atts != INVALID_FILE_ATTRIBUTES && ( atts &  FILE_ATTRIBUTE_DIRECTORY );
#else
#endif
}

string parent_path( const string& str )
{
	if ( str.size() < 3 ) return "";
	size_t pos = str.find_last_of( "\\/" );
	if ( pos != string::npos )
		return str.substr(0, pos );
	return "";
}

string append_path( const string& base, const string& append )
{
	string retval = base;
	if ( retval.size() == 0 ) return retval;

	if ( retval.find_last_of( "\\/" ) != retval.size() - 1 )
		retval.append( "/" );

	retval.append( append );

	return retval;
}

string corpus_dir()
{
	string exec_path( executable_path() );
	string dir = parent_path( exec_path );
	while( !dir.empty() )
	{
		string test = dir;
		test = append_path( test, "corpus" );
		if ( is_directory( test ) )
			return test;
		dir = parent_path( dir );
	}
	throw runtime_error( "Failed to find test dir" );
}

string corpus_file( const char* fname )
{
	auto test_file = corpus_dir();
	test_file = append_path( test_file, fname );
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
	auto compiler_ptr = compiler::create();					\
	float test_result = compiler_ptr->execute( test_data );	\
	ASSERT_EQ( answer, test_result );						\
}

/*
DEFINE_SIMPLE_CORPUS_TEST( basic1, 3.0f );
DEFINE_SIMPLE_CORPUS_TEST( basic2, 8.0f );
DEFINE_SIMPLE_CORPUS_TEST( basic3, 20.0f );
DEFINE_SIMPLE_CORPUS_TEST( basic4, -100.0f );
DEFINE_SIMPLE_CORPUS_TEST( basic_struct, 15.0f );
DEFINE_SIMPLE_CORPUS_TEST( for_loop, 125.0f );
DEFINE_SIMPLE_CORPUS_TEST( numeric_cast, 30.0f );
*/
DEFINE_SIMPLE_CORPUS_TEST( dynamic_mem, 45.0f );



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