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
#else       
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "pcre.h"


using namespace cclj;
using std::cout;
using std::endl;

namespace 
{

string executable_path()
{
#ifdef _WIN32
	char temp_buf[1024];
	GetModuleFileNameA ( NULL, temp_buf, 1024 );
	return temp_buf;
#else
	char buf[1024];
	readlink( "/proc/self/exe", buf, 1024 );
	return buf;
#endif
}

bool is_directory(const string& str )
{
#ifdef _WIN32
	DWORD atts = GetFileAttributesA( str.c_str() );
	return atts != INVALID_FILE_ATTRIBUTES && ( atts &  FILE_ATTRIBUTE_DIRECTORY );
#else
	struct stat st;
	std::memset( &st, 0, sizeof( st ) );
	stat( str.c_str(), &st );
	return S_ISDIR(st.st_mode);
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
	string nameExt( fname );
	nameExt.append( ".cclj" );
	auto filename = corpus_file( nameExt.c_str() );
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

bool run_corpus_test( const char* name, float answer )
{
	auto test_data = corpus_file_text( name );
	auto compiler_ptr = compiler::create();
	float test_result = compiler_ptr->execute( test_data );
	return test_result == answer;
}

}


TEST(corpus_tests, basic1 ) { ASSERT_TRUE( run_corpus_test( "basic1", 3.0f ) ); }
/*
TEST(corpus_tests, basic2 ) { ASSERT_TRUE( run_corpus_test( "basic2", 8.0f ) ); }
TEST(corpus_tests, basic3 ) { ASSERT_TRUE( run_corpus_test( "basic3", 20.0f ) ); }
TEST(corpus_tests, basic4 ) { ASSERT_TRUE( run_corpus_test( "basic4", -100.0f ) ); }
TEST(corpus_tests, basic_struct ) { ASSERT_TRUE( run_corpus_test( "basic_struct", 15.0f ) ); }
TEST(corpus_tests, for_loop ) { ASSERT_TRUE( run_corpus_test( "for_loop", 125.0f ) ); }
TEST(corpus_tests, numeric_cast ) { ASSERT_TRUE( run_corpus_test( "numeric_cast", 30.0f ) ); }
TEST(corpus_tests, dynamic_mem ) { ASSERT_TRUE( run_corpus_test( "dynamic_mem", 45.0f ) ); }
TEST(corpus_tests, poly_fn ) { ASSERT_TRUE(run_corpus_test("poly_fn", 53.0f ) ); }
TEST(corpus_tests, macro_fn2 ) { ASSERT_TRUE(run_corpus_test("macro_fn2", 55.0f ) ); }
TEST(corpus_tests, void_fn ) { ASSERT_TRUE(run_corpus_test("void", 55.0f ) ); }
TEST(corpus_tests, scope_exit) { ASSERT_TRUE(run_corpus_test("scope_exit", 135.0f)); }
TEST(corpus_tests, tuple) { ASSERT_TRUE(run_corpus_test("tuple", 25.0f)); }
*/




TEST(regex_tests, symbol_regex)
{
	const char* errorMsg;
	int errOffset;
	pcre *re = pcre_compile( "^[\\+-]?\\d+\\.?\\d*e?\\d*", 0, &errorMsg, &errOffset, NULL );
	ASSERT_TRUE( re != NULL );

	const char* testStrings[] = {
		"+51",
		"-51",
		"+51.54",
		"-51.54",
		"51e10",
		"51.54e10",
	};
	for ( int idx = 0; idx < 6; ++idx )
	{
		int rc = pcre_exec( re, NULL, testStrings[idx], strlen(testStrings[idx]), 0, 0, NULL, 0 );
		ASSERT_TRUE( rc >= 0 );
	}
	const char* testNegativeStrings[] = {
		"()",
		"one32",
		"f32",
	};
	for ( int idx = 0; idx < 2; ++idx )
	{
		int rc = pcre_exec( re, NULL, testNegativeStrings[idx], strlen(testNegativeStrings[idx]), 0, 0, NULL, 0 );
		ASSERT_TRUE( rc < 0 );
	}

	pcre_free( re );

}
