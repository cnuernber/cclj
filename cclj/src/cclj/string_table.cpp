//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#include "precompile.h"
#include "cclj/string_table.h"
#include "cclj/language_types.h"

using namespace cclj;

namespace 
{
	struct str_table_key
	{
		string str;
		size_t hash_code;

		str_table_key( const string& val ) 
			: str( val )
			, hash_code( std::hash<string>()( str ) )
		{
		}
		bool operator==( const str_table_key& other ) const { return str == other.str; }
		bool operator!=( const str_table_key& other ) const { return str != other.str; }
	};
}

namespace std 
{
	template<> struct hash<str_table_key>
	{
		size_t operator()( const str_table_key& key ) const { return key.hash_code; }
	};
}

namespace 
{
	typedef unordered_map<str_table_key,string> TKeyStrMap;
	struct str_table_impl : public string_table
	{
		TKeyStrMap str_table;
		str_table_impl(){}
		
		virtual string_table_str register_str( const char* data )
		{
			if ( is_trivial( data ) ) { return string_table_str(); }
			str_table_key theKey( data );
			pair<TKeyStrMap::iterator, bool> inserter = str_table.insert( make_pair( theKey, string() ) );
			if ( inserter.second ) inserter.first->second.assign( data );
			return string_table_str::unsafe_create_string_table_str( inserter.first->second.c_str() );
		}
	};
}

string_table_ptr string_table::create() { return make_shared<str_table_impl>(); }