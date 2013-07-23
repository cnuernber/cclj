//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_LISP_H
#define CCLJ_LISP_H
#pragma once

#include "cclj/cclj.h"
#include "cclj/string_table.h"
#include "cclj/data_buffer.h"
#include "cclj/noncopyable.h"
#include "cclj/allocator.h"
#include "cclj/type_library.h"
#include "cclj/lisp_types.h"

namespace cclj
{
	namespace lisp
	{

		class reader
		{
		public:
			
			friend class shared_ptr<reader>;
			//Probably need some erro reporting in there somewhere.
			virtual object_ptr_buffer parse( const string& str ) = 0;

			static shared_ptr<reader> create_reader( factory_ptr factory
														, type_library_ptr type_library
														, string_table_ptr str_table );
		};

		typedef shared_ptr<reader> reader_ptr;


		
		inline vector<string> split_symbol( symbol& sym )
		{
			vector<string> retval;
			string temp(sym._name.c_str());
			size_t last_offset = 0;
			for ( size_t off = temp.find( '.' ); off != string::npos;
				off = temp.find( '.', off+1 ) )
			{
				retval.push_back( temp.substr( last_offset, off - last_offset ) );
				last_offset = off + 1;
			}
			if ( last_offset < temp.size() )
			{
				retval.push_back( temp.substr( last_offset, temp.size() - last_offset ) );
			}
			return retval;
		}
	}
}

#endif