//==============================================================================
//  Copyright 2013, Chris Nuernberger
//	ALL RIGHTS RESERVED
//
//  This code is licensed under the BSD license.  Terms of the
//	license are located under the top cclj directory
//==============================================================================
#ifndef CCLJ_ALGO_UTIL_H
#define CCLJ_ALGO_UTIL_H
#include "cclj.h"
#include "cclj/data_buffer.h"
namespace cclj
{
	template<typename tdtype, typename tfindop>
	inline tdtype find_or_default( const vector<tdtype>& data, tfindop findop, tdtype def = tdtype() )
	{
		auto iter = find_if ( data.begin(), data.end(), findop );
		if ( iter != data.end() )
			return *iter;
		return def;
	}

	template<typename tdtype, typename tfindop>
	inline tdtype find_or_default( const const_data_buffer<tdtype>& data, tfindop findop, tdtype def = tdtype() )
	{
		auto iter = find_if ( data.begin(), data.end(), findop );
		if ( iter != data.end() )
			return *iter;
		return def;
	}

	template<typename tdtype, typename tfindop>
	inline tdtype find_or_default( const data_buffer<tdtype>& data, tfindop findop, tdtype def = tdtype() )
	{
		auto iter = find_if ( data.begin(), data.end(), findop );
		if ( iter != data.end() )
			return *iter;
		return def;
	}

	template<typename trettype, typename tcontainertype, typename tfindop>
	inline trettype find_first_transform( const tcontainertype& container, tfindop findop, trettype retval )
	{
		for ( auto iter =container.begin(), end = container.end(); iter != end && !retval; ++iter )
			retval = findop( *iter );

		return retval;
	}

	template<typename tcontainertype, typename tfindop, typename titemtype>
	inline void insert_or_override( tcontainertype& container, tfindop findop, titemtype item )
	{
		auto iter = find_if( container.begin(), container.end(), findop );
		if (iter != container.end() )
			*iter = item;
		else
			container.insert( iter, item );
	}
	
	template<typename number_type>
	inline number_type align_number( number_type data, uint8_t alignment )
	{
		number_type diff = data % static_cast<number_type>( alignment );
		if ( diff )
			data = data + alignment - diff;
		return data;
	}
}
#endif