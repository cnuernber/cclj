//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_STRING_TABLE_H
#define CCLJ_STRING_TABLE_H
#pragma once
#include "cclj/cclj.h"


namespace cclj 
{
	class string_table;

	inline bool is_trivial( const char* data )
	{
		return data == nullptr || *data == 0;
	}

	inline const char* non_null( const char* data )
	{
		return data ? data : "";
	}

	class string_table_str
	{
		const char* data;
	public:
		string_table_str() : data( "" ) {}
		string_table_str( const string_table_str& other ) : data( other.data ) {}
		string_table_str& operator=( const string_table_str& other )
		{
			data = other.data;
			return *this;
		}

		operator const char* () const { return data; }
		const char* c_str() const { return data; }

		bool empty() const { return data == nullptr || *data == 0; }

		bool operator==( const string_table_str& other )
		{
			return data == other.data;
		}

		bool operator!=( const string_table_str& other )
		{
			return data != other.data;
		}

		//called if you already have a registered string.
		static string_table_str unsafe_create_string_table_str( const char* table_str )
		{
			string_table_str retval;
			retval.data = table_str;
			return retval;
		}
	};

	class string_table
	{
	protected:
		virtual ~string_table(){}
	public:
		virtual string_table_str register_str( const char* data ) = 0;
		string_table_str register_str( const string& data ) { return register_str( data.c_str() ); }

		friend class shared_ptr<string_table>;

		static shared_ptr<string_table> create();
	};

	typedef shared_ptr<string_table> string_table_ptr;
}

//Integration with std::hash so we can use these as hashtable keys
namespace std
{
	template<> struct hash<cclj::string_table_str> 
	{
		size_t operator()( const cclj::string_table_str& str ) 
		{ 
			const char* strData = str;
			return hash<size_t>()( reinterpret_cast<size_t>( strData ) );
		}
	};
}

#endif
